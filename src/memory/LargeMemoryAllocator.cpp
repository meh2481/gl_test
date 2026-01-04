#include "LargeMemoryAllocator.h"
#include "../debug/ConsoleBuffer.h"
#include <cstring>
#include <cassert>

static const size_t MIN_BLOCK_SIZE = 64;
static const size_t ALIGNMENT = 16;
static const float SHRINK_THRESHOLD = 0.25f;
static const size_t DEFAULT_CHUNK_SIZE = 1024 * 1024; // 1 MB

static size_t alignSize(size_t size) {
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

LargeMemoryAllocator::LargeMemoryAllocator()
    : m_chunks(nullptr), m_chunkSize(0), m_totalPoolSize(0), m_usedMemory(0), m_allocationCount(0), m_freeList(nullptr) {
    m_mutex = SDL_CreateMutex();
    assert(m_mutex != nullptr);
#ifdef DEBUG
    historyIndex_ = 0;
    historyCount_ = 0;
    lastSampleTime_ = 0.0f;
    memset(usageHistory_, 0, sizeof(usageHistory_));
#endif
    m_chunkSize = alignSize(DEFAULT_CHUNK_SIZE);
    addChunk(m_chunkSize);
}

LargeMemoryAllocator::~LargeMemoryAllocator() {
    if (m_allocationCount > 0) {
        MemoryChunk* chunk = m_chunks;
        while (chunk) {
            BlockHeader* current = (BlockHeader*)chunk->memory;
            char* chunkEnd = chunk->memory + chunk->size;

            while ((char*)current < chunkEnd) {
                if (!current->isFree) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Leaked block: size=%zu, allocationId=%s",
                                    current->size, current->allocationId ? current->allocationId : "unknown");
                }

                BlockHeader* next = (BlockHeader*)((char*)current + sizeof(BlockHeader) + current->size);
                if ((char*)next >= chunkEnd) {
                    break;
                }
                current = next;
            }
            chunk = chunk->next;
        }
    }
    assert(m_allocationCount == 0);

    MemoryChunk* chunk = m_chunks;
    while (chunk) {
        MemoryChunk* next = chunk->next;
        SDL_free(chunk->memory);
        SDL_free(chunk);
        chunk = next;
    }
    if (m_mutex) {
        SDL_DestroyMutex(m_mutex);
    }
}

void LargeMemoryAllocator::addChunk(size_t size) {
    size_t chunkSize = size < m_chunkSize ? m_chunkSize : size;
    chunkSize = alignSize(chunkSize);

    // If we're creating a chunk significantly larger than our current chunk size,
    // grow m_chunkSize to avoid creating many small chunks later
    // This prevents the pattern of having one large chunk and then many small 1MB chunks
    if (chunkSize > m_chunkSize) {
        // Grow chunk size more aggressively to match the new allocation pattern
        // Use the larger of: (current * 2) or (new chunk size)
        size_t newChunkSize = chunkSize;
        if (m_chunkSize * 2 > newChunkSize) {
            newChunkSize = m_chunkSize * 2;
        }
        // Cap at 32MB to avoid excessive growth
        if (newChunkSize > 32 * 1024 * 1024) {
            newChunkSize = 32 * 1024 * 1024;
        }
        m_chunkSize = alignSize(newChunkSize);
    }

    MemoryChunk* newChunk = (MemoryChunk*)SDL_malloc(sizeof(MemoryChunk));
    assert(newChunk != nullptr);

    newChunk->memory = (char*)SDL_malloc(chunkSize);
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
    block->allocationId = nullptr;

    if (m_freeList) {
        m_freeList->prev = block;
    }
    m_freeList = block;
    newChunk->firstBlock = block;
}

void* LargeMemoryAllocator::allocate(size_t size, const char* allocationId) {
    SDL_LockMutex(m_mutex);

    assert(size > 0);
    assert(allocationId != nullptr);
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
    block->allocationId = allocationId;
    m_usedMemory += block->size + sizeof(BlockHeader);
    m_allocationCount++;

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
    m_allocationCount--;
    block->isFree = true;
    block->allocationId = nullptr;

    // Set up temporary linkage for merge detection, but don't add to free list yet
    block->next = nullptr;
    block->prev = nullptr;

    // Merge with adjacent blocks first - this may change which block we add to free list
    BlockHeader* finalBlock = mergeAdjacentBlocks(block);

    // Always add the final block to the free list, removing it first if it's already there
    // This handles the case where we merged with a block that was already in the free list
    if (finalBlock != block) {
        // We merged with a previous block - it should already be in the free list
        // But let's verify and re-add to be safe
        // First, try to remove it from the free list if it's there
        if (finalBlock->prev) {
            finalBlock->prev->next = finalBlock->next;
        }
        if (finalBlock->next) {
            finalBlock->next->prev = finalBlock->prev;
        }
        if (m_freeList == finalBlock) {
            m_freeList = finalBlock->next;
        }
    }
    
    // Now add the final block to the front of the free list
    finalBlock->next = m_freeList;
    finalBlock->prev = nullptr;
    if (m_freeList) {
        m_freeList->prev = finalBlock;
    }
    m_freeList = finalBlock;

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

        // Remove chunk if it's completely free
        // Keep at least one chunk to avoid constant allocation/deallocation
        bool isEmpty = (block->isFree &&
                       block->size == chunk->size - sizeof(BlockHeader));
        
        // Only remove if not the last chunk
        bool isLastChunk = (m_chunks == chunk && chunk->next == nullptr);

        if (isEmpty && !isLastChunk) {
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
            SDL_free(chunk->memory);
            SDL_free(chunk);
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
    newBlock->allocationId = nullptr;

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
            chunkInfo[i].blocks[j].allocationId = block->allocationId;

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

LargeMemoryAllocator::AllocationStats* LargeMemoryAllocator::getAllocationStats(size_t* outStatsCount) const {
    SDL_LockMutex(m_mutex);

    // First pass: count unique allocation IDs
    size_t maxStats = 256; // Initial capacity
    AllocationStats* tempStats = new AllocationStats[maxStats];
    size_t statsCount = 0;

    // Iterate through all chunks and blocks to collect stats
    MemoryChunk* chunk = m_chunks;
    while (chunk) {
        char* chunkEnd = chunk->memory + chunk->size;
        BlockHeader* block = chunk->firstBlock;

        while ((char*)block < chunkEnd) {
            if (!block->isFree && block->allocationId != nullptr) {
                // Find or create entry for this allocation ID
                size_t idx = 0;
                for (idx = 0; idx < statsCount; idx++) {
                    if (tempStats[idx].allocationId == block->allocationId) {
                        break;
                    }
                }

                if (idx == statsCount) {
                    // New allocation ID
                    if (statsCount >= maxStats) {
                        // Need to grow the array
                        size_t newMaxStats = maxStats * 2;
                        AllocationStats* newTempStats = new AllocationStats[newMaxStats];
                        memcpy(newTempStats, tempStats, statsCount * sizeof(AllocationStats));
                        delete[] tempStats;
                        tempStats = newTempStats;
                        maxStats = newMaxStats;
                    }
                    tempStats[statsCount].allocationId = block->allocationId;
                    tempStats[statsCount].count = 1;
                    tempStats[statsCount].totalBytes = block->size;
                    statsCount++;
                } else {
                    // Existing allocation ID
                    tempStats[idx].count++;
                    tempStats[idx].totalBytes += block->size;
                }
            }

            // Move to next block
            BlockHeader* nextBlock = (BlockHeader*)((char*)block + sizeof(BlockHeader) + block->size);
            block = nextBlock;
        }

        chunk = chunk->next;
    }

    // Create right-sized output array
    AllocationStats* stats = nullptr;
    if (statsCount > 0) {
        stats = new AllocationStats[statsCount];
        memcpy(stats, tempStats, statsCount * sizeof(AllocationStats));
    }
    delete[] tempStats;

    *outStatsCount = statsCount;
    SDL_UnlockMutex(m_mutex);
    return stats;
}

void LargeMemoryAllocator::freeAllocationStats(AllocationStats* stats, size_t statsCount) const {
    (void)statsCount; // Unused
    delete[] stats;
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


