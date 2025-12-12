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
    m_mutex = SDL_CreateMutex();
    assert(m_mutex != nullptr);
#ifdef DEBUG
    historyIndex_ = 0;
    historyCount_ = 0;
    lastSampleTime_ = 0.0f;
    memset(usageHistory_, 0, sizeof(usageHistory_));
#endif
    m_chunkSize = alignSize(initialChunkSize);
    addChunk(m_chunkSize);
}

LargeMemoryAllocator::~LargeMemoryAllocator() {
    MemoryChunk* chunk = m_chunks;
    while (chunk) {
        MemoryChunk* next = chunk->next;
        std::free(chunk->memory);
        std::free(chunk);
        chunk = next;
    }
    if (m_mutex) {
        SDL_DestroyMutex(m_mutex);
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
}

void* LargeMemoryAllocator::allocate(size_t size) {
    SDL_LockMutex(m_mutex);

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

    void* ptr = (char*)block + sizeof(BlockHeader);
    SDL_UnlockMutex(m_mutex);
    return ptr;
}

void LargeMemoryAllocator::free(void* ptr) {
    assert(ptr != nullptr);

    SDL_LockMutex(m_mutex);

    BlockHeader* block = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
    assert(!block->isFree);
    assert(findChunkForPointer(ptr) != nullptr);

    m_usedMemory -= block->size + sizeof(BlockHeader);
    block->isFree = true;

    // Set up temporary linkage for merge detection, but don't add to free list yet
    block->next = nullptr;
    block->prev = nullptr;

    // Merge with adjacent blocks first - this may change which block we add to free list
    BlockHeader* finalBlock = mergeAdjacentBlocks(block);

    // Only add to free list if the final block is not already in it
    // (If we merged with a previous block, that block is already in the free list)
    if (finalBlock == block) {
        // This is a new block (not merged with a previous one), add it to free list
        finalBlock->next = m_freeList;
        finalBlock->prev = nullptr;
        if (m_freeList) {
            m_freeList->prev = finalBlock;
        }
        m_freeList = finalBlock;
    }

    if (m_totalPoolSize > 0 && (float)m_usedMemory / m_totalPoolSize < SHRINK_THRESHOLD && m_totalPoolSize > m_chunkSize) {
        removeEmptyChunks();
    }

    SDL_UnlockMutex(m_mutex);
}

size_t LargeMemoryAllocator::defragment() {
    SDL_LockMutex(m_mutex);

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

    SDL_UnlockMutex(m_mutex);
    return mergedBlocks;
}

size_t LargeMemoryAllocator::getTotalMemory() const {
    SDL_LockMutex(m_mutex);
    size_t result = m_totalPoolSize;
    SDL_UnlockMutex(m_mutex);
    return result;
}

size_t LargeMemoryAllocator::getUsedMemory() const {
    SDL_LockMutex(m_mutex);
    size_t result = m_usedMemory;
    SDL_UnlockMutex(m_mutex);
    return result;
}

size_t LargeMemoryAllocator::getFreeMemory() const {
    SDL_LockMutex(m_mutex);
    size_t result = m_totalPoolSize - m_usedMemory;
    SDL_UnlockMutex(m_mutex);
    return result;
}

void LargeMemoryAllocator::removeEmptyChunks() {
    MemoryChunk** chunkPtr = &m_chunks;
    while (*chunkPtr) {
        MemoryChunk* chunk = *chunkPtr;
        BlockHeader* block = (BlockHeader*)chunk->memory;

        bool isEmpty = (block->isFree &&
                       block->size == chunk->size - sizeof(BlockHeader) &&
                       chunk != m_chunks);

        if (isEmpty) {
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

LargeMemoryAllocator::BlockHeader* LargeMemoryAllocator::mergeAdjacentBlocks(BlockHeader* block) {
    assert(block != nullptr);
    assert(block->isFree);

    MemoryChunk* chunk = block->chunk;
    char* chunkEnd = chunk->memory + chunk->size;
    BlockHeader* result = block;

    // Merge with next block if it's free and adjacent
    BlockHeader* next = (BlockHeader*)((char*)block + sizeof(BlockHeader) + block->size);
    if ((char*)next < chunkEnd && next->isFree && next->chunk == chunk) {
        block->size += sizeof(BlockHeader) + next->size;

        // Remove next block from free list
        if (next->prev) {
            next->prev->next = next->next;
        }
        if (next->next) {
            next->next->prev = next->prev;
        }
        if (m_freeList == next) {
            m_freeList = next->next;
        }
    }

    // Merge with previous block if it's free and adjacent
    // Only need to check if we're not at the start of the chunk
    if ((char*)block > chunk->memory) {
        BlockHeader* current = (BlockHeader*)chunk->memory;
        while ((char*)current < (char*)block) {
            BlockHeader* nextBlock = (BlockHeader*)((char*)current + sizeof(BlockHeader) + current->size);

            // Found the block immediately before us
            if (nextBlock == block && current->isFree && current->chunk == chunk) {
                current->size += sizeof(BlockHeader) + block->size;
                // Return the previous block as the result since it absorbed our block
                result = current;
                break;
            }

            // Move to next block - add bounds check to prevent infinite loop
            if ((char*)nextBlock >= chunkEnd || (char*)nextBlock <= (char*)current) {
                break;
            }
            current = nextBlock;
        }
    }

    return result;
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

#ifdef DEBUG
LargeMemoryAllocator::ChunkInfo* LargeMemoryAllocator::getChunkInfo(size_t* outChunkCount) const {
    SDL_LockMutex(m_mutex);

    // Count chunks
    size_t chunkCount = 0;
    MemoryChunk* chunk = m_chunks;
    while (chunk) {
        chunkCount++;
        chunk = chunk->next;
    }

    *outChunkCount = chunkCount;
    if (chunkCount == 0) {
        SDL_UnlockMutex(m_mutex);
        return nullptr;
    }

    // Allocate chunk info array
    ChunkInfo* chunkInfo = new ChunkInfo[chunkCount];

    // Fill in chunk information
    chunk = m_chunks;
    for (size_t i = 0; i < chunkCount; i++) {
        assert(chunk != nullptr);

        chunkInfo[i].size = chunk->size;

        // Count blocks in this chunk by traversing the memory
        size_t blockCount = 0;
        BlockHeader* block = (BlockHeader*)chunk->memory;
        char* chunkEnd = chunk->memory + chunk->size;

        while ((char*)block < chunkEnd) {
            blockCount++;
            BlockHeader* nextBlock = (BlockHeader*)((char*)block + sizeof(BlockHeader) + block->size);
            if ((char*)nextBlock >= chunkEnd) {
                break;
            }
            block = nextBlock;
        }

        chunkInfo[i].blockCount = blockCount;
        chunkInfo[i].blocks = new BlockInfo[blockCount];

        // Fill in block information
        block = (BlockHeader*)chunk->memory;
        for (size_t j = 0; j < blockCount; j++) {
            chunkInfo[i].blocks[j].offset = (char*)block - chunk->memory;
            chunkInfo[i].blocks[j].size = block->size;
            chunkInfo[i].blocks[j].isFree = block->isFree;

            BlockHeader* nextBlock = (BlockHeader*)((char*)block + sizeof(BlockHeader) + block->size);
            if ((char*)nextBlock >= chunkEnd) {
                break;
            }
            block = nextBlock;
        }

        chunk = chunk->next;
    }

    SDL_UnlockMutex(m_mutex);
    return chunkInfo;
}

void LargeMemoryAllocator::freeChunkInfo(ChunkInfo* chunkInfo, size_t chunkCount) const {
    if (chunkInfo) {
        for (size_t i = 0; i < chunkCount; i++) {
            delete[] chunkInfo[i].blocks;
        }
        delete[] chunkInfo;
    }
}

void LargeMemoryAllocator::getUsageHistory(size_t* outHistory, size_t* outCount) const {
    SDL_LockMutex(m_mutex);

    *outCount = historyCount_;
    if (historyCount_ > 0) {
        // Copy history in chronological order
        size_t count = historyCount_ < HISTORY_SIZE ? historyCount_ : HISTORY_SIZE;
        if (historyCount_ < HISTORY_SIZE) {
            // Haven't wrapped around yet
            memcpy(outHistory, usageHistory_, count * sizeof(size_t));
        } else {
            // Wrapped around - need to copy in two parts
            size_t firstPart = HISTORY_SIZE - historyIndex_;
            memcpy(outHistory, &usageHistory_[historyIndex_], firstPart * sizeof(size_t));
            if (historyIndex_ > 0) {
                memcpy(&outHistory[firstPart], usageHistory_, historyIndex_ * sizeof(size_t));
            }
        }
    }

    SDL_UnlockMutex(m_mutex);
}

void LargeMemoryAllocator::updateMemoryHistory(float currentTime) {
    SDL_LockMutex(m_mutex);

    // Only sample if enough time has passed
    if (currentTime - lastSampleTime_ < SAMPLE_INTERVAL) {
        SDL_UnlockMutex(m_mutex);
        return;
    }

    lastSampleTime_ = currentTime;

    // Record current memory usage
    usageHistory_[historyIndex_] = m_usedMemory;
    historyIndex_ = (historyIndex_ + 1) % HISTORY_SIZE;
    if (historyCount_ < HISTORY_SIZE) {
        historyCount_++;
    }

    SDL_UnlockMutex(m_mutex);
}

size_t LargeMemoryAllocator::getBlockHeaderSize() {
    return sizeof(BlockHeader);
}
#endif


