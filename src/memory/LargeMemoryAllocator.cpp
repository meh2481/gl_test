#include "LargeMemoryAllocator.h"
#include <cstring>
#include <cassert>
#include <iostream>
#include <cstdlib>

static const size_t MIN_BLOCK_SIZE = 64;
static const size_t ALIGNMENT = 16;
static const float SHRINK_THRESHOLD = 0.25f;

static size_t alignSize(size_t size) {
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

LargeMemoryAllocator::LargeMemoryAllocator(size_t initialChunkSize)
    : m_chunks(nullptr), m_chunkSize(0), m_totalPoolSize(0), m_usedMemory(0), m_freeList(nullptr) {
    assert(initialChunkSize > 0);
    m_chunkSize = alignSize(initialChunkSize);
    addChunk(m_chunkSize);
    std::cout << "LargeMemoryAllocator: Initialized with chunk size " << m_chunkSize << " bytes" << std::endl;
}

LargeMemoryAllocator::~LargeMemoryAllocator() {
    MemoryChunk* chunk = m_chunks;
    while (chunk) {
        MemoryChunk* next = chunk->next;
        std::cout << "LargeMemoryAllocator: Deallocating chunk of " << chunk->size << " bytes" << std::endl;
        std::free(chunk->memory);
        std::free(chunk);
        chunk = next;
    }
}

void LargeMemoryAllocator::addChunk(size_t size) {
    size_t chunkSize = size < m_chunkSize ? m_chunkSize : size;
    chunkSize = alignSize(chunkSize);

    MemoryChunk* newChunk = (MemoryChunk*)std::malloc(sizeof(MemoryChunk));
    assert(newChunk != nullptr);

    newChunk->memory = (char*)std::malloc(chunkSize);
    assert(newChunk->memory != nullptr);

    newChunk->size = chunkSize;
    newChunk->next = m_chunks;
    m_chunks = newChunk;
    m_totalPoolSize += chunkSize;

    BlockHeader* block = (BlockHeader*)newChunk->memory;
    block->size = chunkSize - sizeof(BlockHeader);
    block->isFree = true;
    block->next = m_freeList;
    block->prev = nullptr;
    block->chunk = newChunk;

    if (m_freeList) {
        m_freeList->prev = block;
    }
    m_freeList = block;
    newChunk->firstBlock = block;

    std::cout << "LargeMemoryAllocator: Added chunk of " << chunkSize << " bytes (total pool: "
              << m_totalPoolSize << ")" << std::endl;
}

void* LargeMemoryAllocator::allocate(size_t size) {
    assert(size > 0);
    size_t alignedSize = alignSize(size);

    BlockHeader* block = findFreeBlock(alignedSize);
    if (!block) {
        size_t newChunkSize = alignedSize + sizeof(BlockHeader);
        if (newChunkSize < m_chunkSize) {
            newChunkSize = m_chunkSize;
        } else {
            newChunkSize = alignSize(newChunkSize * 2);
        }
        addChunk(newChunkSize);
        block = findFreeBlock(alignedSize);
        assert(block != nullptr);
    }

    if (block->size >= alignedSize + sizeof(BlockHeader) + MIN_BLOCK_SIZE) {
        splitBlock(block, alignedSize);
    }

    block->isFree = false;
    m_usedMemory += block->size + sizeof(BlockHeader);

    if (m_freeList == block) {
        m_freeList = block->next;
    }
    if (block->prev) {
        block->prev->next = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    }

    // Clear pointers to prevent cycles in free list
    block->next = nullptr;
    block->prev = nullptr;

    void* ptr = (char*)block + sizeof(BlockHeader);
    std::cout << "LargeMemoryAllocator: Allocated " << alignedSize << " bytes at " << ptr
              << " (used: " << m_usedMemory << "/" << m_totalPoolSize << ")" << std::endl;
    return ptr;
}

void LargeMemoryAllocator::free(void* ptr) {
    assert(ptr != nullptr);

    BlockHeader* block = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
    assert(!block->isFree);
    assert(findChunkForPointer(ptr) != nullptr);

    m_usedMemory -= block->size + sizeof(BlockHeader);
    block->isFree = true;

    block->next = m_freeList;
    block->prev = nullptr;
    if (m_freeList) {
        m_freeList->prev = block;
    }
    m_freeList = block;

    mergeAdjacentBlocks(block);

    if (m_totalPoolSize > 0 && (float)m_usedMemory / m_totalPoolSize < SHRINK_THRESHOLD && m_totalPoolSize > m_chunkSize) {
        removeEmptyChunks();
    }
}

size_t LargeMemoryAllocator::defragment() {
    std::cout << "LargeMemoryAllocator: Starting defragmentation..." << std::endl;

    size_t mergedBlocks = 0;
    MemoryChunk* chunk = m_chunks;
    while (chunk) {
        BlockHeader* current = (BlockHeader*)chunk->memory;
        char* chunkEnd = chunk->memory + chunk->size;

        while ((char*)current < chunkEnd) {
            if (current->isFree) {
                BlockHeader* next = (BlockHeader*)((char*)current + sizeof(BlockHeader) + current->size);
                if ((char*)next < chunkEnd && next->isFree && next->chunk == chunk) {
                    current->size += sizeof(BlockHeader) + next->size;

                    if (next->prev) {
                        next->prev->next = next->next;
                    }
                    if (next->next) {
                        next->next->prev = next->prev;
                    }
                    if (m_freeList == next) {
                        m_freeList = next->prev ? next->prev : next->next;
                    }
                    mergedBlocks++;
                    continue;
                }
            }

            BlockHeader* next = (BlockHeader*)((char*)current + sizeof(BlockHeader) + current->size);
            if ((char*)next >= chunkEnd) {
                break;
            }
            current = next;
        }
        chunk = chunk->next;
    }

    std::cout << "LargeMemoryAllocator: Defragmentation complete, merged " << mergedBlocks << " blocks" << std::endl;
    return mergedBlocks;
}

void LargeMemoryAllocator::removeEmptyChunks() {
    std::cout << "LargeMemoryAllocator: Checking for empty chunks to remove..." << std::endl;

    MemoryChunk** chunkPtr = &m_chunks;
    while (*chunkPtr) {
        MemoryChunk* chunk = *chunkPtr;
        BlockHeader* block = (BlockHeader*)chunk->memory;

        bool isEmpty = (block->isFree &&
                       block->size == chunk->size - sizeof(BlockHeader) &&
                       chunk != m_chunks);

        if (isEmpty) {
            std::cout << "LargeMemoryAllocator: Removing empty chunk of " << chunk->size << " bytes" << std::endl;

            if (block->prev) {
                block->prev->next = block->next;
            }
            if (block->next) {
                block->next->prev = block->prev;
            }
            if (m_freeList == block) {
                m_freeList = block->next;
            }

            *chunkPtr = chunk->next;
            m_totalPoolSize -= chunk->size;
            std::free(chunk->memory);
            std::free(chunk);
        } else {
            chunkPtr = &chunk->next;
        }
    }
}

LargeMemoryAllocator::BlockHeader* LargeMemoryAllocator::findFreeBlock(size_t size) {
    BlockHeader* bestFit = nullptr;
    size_t bestFitSize = SIZE_MAX;

    BlockHeader* current = m_freeList;
    while (current) {
        if (current->isFree && current->size >= size) {
            if (current->size < bestFitSize) {
                bestFit = current;
                bestFitSize = current->size;
                if (bestFitSize == size) {
                    break;
                }
            }
        }
        current = current->next;
    }

    return bestFit;
}

void LargeMemoryAllocator::splitBlock(BlockHeader* block, size_t size) {
    assert(block != nullptr);
    assert(block->isFree);
    assert(block->size >= size + sizeof(BlockHeader) + MIN_BLOCK_SIZE);

    BlockHeader* newBlock = (BlockHeader*)((char*)block + sizeof(BlockHeader) + size);
    newBlock->size = block->size - size - sizeof(BlockHeader);
    newBlock->isFree = true;
    newBlock->chunk = block->chunk;
    newBlock->next = block->next;
    newBlock->prev = block;

    if (block->next) {
        block->next->prev = newBlock;
    }
    block->next = newBlock;
    block->size = size;
}

void LargeMemoryAllocator::mergeAdjacentBlocks(BlockHeader* block) {
    assert(block != nullptr);
    assert(block->isFree);

    MemoryChunk* chunk = block->chunk;
    char* chunkEnd = chunk->memory + chunk->size;

    BlockHeader* next = (BlockHeader*)((char*)block + sizeof(BlockHeader) + block->size);
    if ((char*)next < chunkEnd && next->isFree && next->chunk == chunk) {
        block->size += sizeof(BlockHeader) + next->size;

        if (next->prev) {
            next->prev->next = next->next;
        }
        if (next->next) {
            next->next->prev = next->prev;
        }
        if (m_freeList == next) {
            m_freeList = next->prev ? next->prev : next->next;
        }
        block->next = next->next;
    }

    BlockHeader* current = (BlockHeader*)chunk->memory;
    while ((char*)current < chunkEnd) {
        BlockHeader* nextBlock = (BlockHeader*)((char*)current + sizeof(BlockHeader) + current->size);
        if (nextBlock == block && current->isFree && current->chunk == chunk) {
            current->size += sizeof(BlockHeader) + block->size;

            // Remove block from free list
            if (block->prev && block->prev != current) {
                block->prev->next = block->next;
            }
            if (block->next && block->next != current) {
                block->next->prev = block->prev ? block->prev : current;
            }
            if (m_freeList == block) {
                m_freeList = current;
            }
            
            // Update current to take block's position in free list
            // But if block->next == current, we're merging adjacent blocks in the list,
            // so current should keep its original next to avoid creating a self-loop
            if (block->next == current) {
                if (current->next) {
                    current->next->prev = current;
                }
            } else {
                current->next = block->next;
                if (block->next) {
                    block->next->prev = current;
                }
            }
            break;
        }

        if ((char*)nextBlock >= chunkEnd) {
            break;
        }
        current = nextBlock;
    }
}

LargeMemoryAllocator::MemoryChunk* LargeMemoryAllocator::findChunkForPointer(void* ptr) const {
    MemoryChunk* chunk = m_chunks;
    while (chunk) {
        if (ptr >= chunk->memory && ptr < chunk->memory + chunk->size) {
            return chunk;
        }
        chunk = chunk->next;
    }
    return nullptr;
}
