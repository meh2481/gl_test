#include "SmallMemoryAllocator.h"
#include "../debug/ConsoleBuffer.h"
#include <cassert>
#include <SDL3/SDL_log.h>

SmallMemoryAllocator::SmallMemoryAllocator()
    : firstPool_(nullptr)
    , lastPool_(nullptr)
    , allocationCount_(0)
    , totalCapacity_(0)
{
    mutex_ = SDL_CreateMutex();
    assert(mutex_ != nullptr);
#ifdef DEBUG
    historyIndex_ = 0;
    historyCount_ = 0;
    lastSampleTime_ = 0.0f;
    SDL_memset(usageHistory_, 0, sizeof(usageHistory_));
#endif
    // Note: Cannot log here as ConsoleBuffer doesn't exist yet
    // Create initial pool
    createPool(MIN_POOL_SIZE);
}

SmallMemoryAllocator::~SmallMemoryAllocator() {
    // Count pools
    Uint64 poolCount = 0;
    MemoryPool* pool = firstPool_;
    while (pool) {
        poolCount++;
        pool = pool->next;
    }

    if (allocationCount_ > 0) {
        pool = firstPool_;
        while (pool) {
            BlockHeader* current = pool->firstBlock;
            while (current) {
                if (!current->isFree) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Leaked block: size=%zu, allocationId=%s",
                                current->size, current->allocationId ? current->allocationId : "unknown");
                }
                current = current->next;
            }
            pool = pool->next;
        }
    }
    assert(allocationCount_ == 0);

    // Free all pools
    pool = firstPool_;
    while (pool) {
        MemoryPool* next = pool->next;
        SDL_free(pool->memory);
        delete pool;
        pool = next;
    }

    if (mutex_) {
        SDL_DestroyMutex(mutex_);
    }
}

void* SmallMemoryAllocator::allocate(Uint64 size, const char* allocationId) {
    SDL_LockMutex(mutex_);

    assert(size > 0);
    assert(allocationId != nullptr);

    // Align size to 8 bytes for better cache performance
    Uint64 alignedSize = (size + 7) & ~7;

    // Try to find a free block in existing pools
    BlockHeader* block = findFreeBlock(alignedSize);

    if (!block) {
        // Need to create a new pool
        Uint64 neededSize = sizeof(BlockHeader) + alignedSize;
        Uint64 newPoolSize = MIN_POOL_SIZE;

        // If we need more than MIN_POOL_SIZE, round up to next power of 2
        while (newPoolSize < neededSize) {
            newPoolSize *= 2;
        }

        // Make new pool at least 2x the last pool size for exponential growth
        if (lastPool_) {
            Uint64 minNewSize = lastPool_->capacity * 2;
            if (newPoolSize < minNewSize) {
                newPoolSize = minNewSize;
            }
        }

        MemoryPool* newPool = createPool(newPoolSize);

        // Try again in the new pool
        block = findFreeBlock(alignedSize);
        assert(block != nullptr);
    }

    // Mark block as used
    block->isFree = false;
    block->allocationId = allocationId;
    allocationCount_++;
    block->pool->allocCount++;

    // Split block if it's much larger than needed
    splitBlock(block, alignedSize);

    // Return pointer after header
    void* ptr = (char*)block + sizeof(BlockHeader);
    SDL_UnlockMutex(mutex_);
    return ptr;
}

void SmallMemoryAllocator::free(void* ptr) {
    if (!ptr) return;

    SDL_LockMutex(mutex_);

    // Get block header
    BlockHeader* block = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
    assert(!block->isFree);
    assert(block->pool != nullptr);

    // Mark as free
    block->isFree = true;
    allocationCount_--;
    block->pool->allocCount--;

    // Coalesce adjacent free blocks in this pool
    coalescePool(block->pool);

    // Remove empty pools
    removeEmptyPools();

    SDL_UnlockMutex(mutex_);
}

Uint64 SmallMemoryAllocator::defragment() {
    SDL_LockMutex(mutex_);

    // Coalesce free blocks in each pool
    Uint64 totalCoalesced = 0;
    MemoryPool* pool = firstPool_;
    while (pool) {
        BlockHeader* current = pool->firstBlock;
        while (current && current->next) {
            // Only coalesce within the same pool
            if (current->isFree && current->next->isFree && current->next->pool == pool) {
                BlockHeader* next = current->next;

                // Expand current block
                current->size += sizeof(BlockHeader) + next->size;
                current->next = next->next;

                if (next->next) {
                    next->next->prev = current;
                }

                if (next == pool->lastBlock) {
                    pool->lastBlock = current;
                }

                pool->used -= sizeof(BlockHeader);
                totalCoalesced++;
            } else {
                current = current->next;
            }
        }
        pool = pool->next;
    }

    // Remove empty pools after coalescing
    removeEmptyPools();

    SDL_UnlockMutex(mutex_);
    return totalCoalesced;
}

SmallMemoryAllocator::MemoryPool* SmallMemoryAllocator::createPool(Uint64 capacity) {
    // Allocate pool structure
    MemoryPool* pool = new MemoryPool();
    pool->memory = (char*)SDL_malloc(capacity);
    assert(pool->memory != nullptr);

    pool->capacity = capacity;
    pool->used = capacity; // Initially all space is in the free block
    pool->allocCount = 0;
    pool->next = nullptr;

    // Create initial free block spanning entire pool
    BlockHeader* freeBlock = (BlockHeader*)pool->memory;
    freeBlock->size = capacity - sizeof(BlockHeader);
    freeBlock->isFree = true;
    freeBlock->next = nullptr;
    freeBlock->prev = nullptr;
    freeBlock->pool = pool;
    freeBlock->allocationId = nullptr;

    pool->firstBlock = freeBlock;
    pool->lastBlock = freeBlock;

    // Add pool to list
    if (!firstPool_) {
        firstPool_ = pool;
        lastPool_ = pool;
    } else {
        lastPool_->next = pool;
        lastPool_ = pool;
    }

    totalCapacity_ += capacity;

    return pool;
}

void SmallMemoryAllocator::removeEmptyPools() {
    MemoryPool* pool = firstPool_;
    MemoryPool* prev = nullptr;

    while (pool) {
        MemoryPool* next = pool->next;

        // Remove pool if it has no active allocations and it's not the only pool
        if (pool->allocCount == 0 && (firstPool_ != lastPool_)) {
            // Unlink from list
            if (prev) {
                prev->next = next;
            } else {
                firstPool_ = next;
            }

            if (pool == lastPool_) {
                lastPool_ = prev;
            }

            totalCapacity_ -= pool->capacity;
            SDL_free(pool->memory);
            delete pool;
        } else {
            prev = pool;
        }

        pool = next;
    }
}

void SmallMemoryAllocator::coalescePool(MemoryPool* pool) {
    if (!pool || !pool->firstBlock) return;

    BlockHeader* current = pool->firstBlock;

    while (current && current->next) {
        // Only coalesce within the same pool
        if (current->isFree && current->next->isFree && current->next->pool == pool) {
            BlockHeader* next = current->next;

            // Expand current block
            current->size += sizeof(BlockHeader) + next->size;
            current->next = next->next;

            if (next->next) {
                next->next->prev = current;
            }

            if (next == pool->lastBlock) {
                pool->lastBlock = current;
            }

            pool->used -= sizeof(BlockHeader);
        } else {
            current = current->next;
        }
    }
}

SmallMemoryAllocator::BlockHeader* SmallMemoryAllocator::findFreeBlock(Uint64 size) {
    // Search all pools for a suitable free block
    MemoryPool* pool = firstPool_;

    while (pool) {
        BlockHeader* current = pool->firstBlock;

        // First-fit strategy within this pool
        while (current) {
            if (current->isFree && current->size >= size) {
                return current;
            }
            current = current->next;
        }

        pool = pool->next;
    }

    return nullptr;
}

void SmallMemoryAllocator::splitBlock(BlockHeader* block, Uint64 size) {
    assert(block != nullptr);
    assert(!block->isFree);
    assert(block->size >= size);
    assert(block->pool != nullptr);

    // Only split if remaining space is worth creating a new block
    Uint64 remainingSize = block->size - size;
    if (remainingSize >= sizeof(BlockHeader) + 8) {
        MemoryPool* pool = block->pool;

        // Create new free block from the remainder
        BlockHeader* newBlock = (BlockHeader*)((char*)block + sizeof(BlockHeader) + size);
        newBlock->size = remainingSize - sizeof(BlockHeader);
        newBlock->isFree = true;
        newBlock->next = block->next;
        newBlock->prev = block;
        newBlock->pool = pool;
        newBlock->allocationId = nullptr;

        if (block->next) {
            block->next->prev = newBlock;
        }
        block->next = newBlock;

        if (block == pool->lastBlock) {
            pool->lastBlock = newBlock;
        }

        // Shrink current block
        block->size = size;
        // Note: pool->used doesn't change - we're just reorganizing existing space
    }
}

#ifdef DEBUG
Uint64 SmallMemoryAllocator::getTotalMemory() const {
    SDL_LockMutex(mutex_);
    Uint64 result = totalCapacity_;
    SDL_UnlockMutex(mutex_);
    return result;
}

Uint64 SmallMemoryAllocator::getUsedMemory() const {
    SDL_LockMutex(mutex_);
    Uint64 used = calculateUsedMemoryLocked();
    SDL_UnlockMutex(mutex_);
    return used;
}

Uint64 SmallMemoryAllocator::getFreeMemory() const {
    SDL_LockMutex(mutex_);
    Uint64 used = calculateUsedMemoryLocked();
    Uint64 result = totalCapacity_ > used ? totalCapacity_ - used : 0;
    SDL_UnlockMutex(mutex_);
    return result;
}

Uint64 SmallMemoryAllocator::getAllocationCount() const {
    SDL_LockMutex(mutex_);
    Uint64 result = allocationCount_;
    SDL_UnlockMutex(mutex_);
    return result;
}

SmallMemoryAllocator::MemoryPoolInfo* SmallMemoryAllocator::getPoolInfo(Uint64* outPoolCount) const {
    SDL_LockMutex(mutex_);

    // Count pools
    Uint64 poolCount = 0;
    MemoryPool* pool = firstPool_;
    while (pool) {
        poolCount++;
        pool = pool->next;
    }

    *outPoolCount = poolCount;
    if (poolCount == 0) {
        SDL_UnlockMutex(mutex_);
        return nullptr;
    }

    // Allocate pool info array
    MemoryPoolInfo* poolInfo = new MemoryPoolInfo[poolCount];

    // Fill in pool information
    pool = firstPool_;
    for (Uint64 i = 0; i < poolCount; i++) {
        assert(pool != nullptr);

        poolInfo[i].capacity = pool->capacity;
        poolInfo[i].used = pool->used;
        poolInfo[i].allocCount = pool->allocCount;

        // Count blocks in this pool
        Uint64 blockCount = 0;
        BlockHeader* block = pool->firstBlock;
        while (block) {
            blockCount++;
            block = block->next;
        }

        poolInfo[i].blockCount = blockCount;
        poolInfo[i].blocks = new BlockInfo[blockCount];

        // Fill in block information
        block = pool->firstBlock;
        for (Uint64 j = 0; j < blockCount; j++) {
            assert(block != nullptr);
            poolInfo[i].blocks[j].offset = (char*)block - pool->memory;
            poolInfo[i].blocks[j].size = block->size;
            poolInfo[i].blocks[j].isFree = block->isFree;
            poolInfo[i].blocks[j].allocationId = block->allocationId;
            block = block->next;
        }

        pool = pool->next;
    }

    SDL_UnlockMutex(mutex_);
    return poolInfo;
}

void SmallMemoryAllocator::freePoolInfo(MemoryPoolInfo* poolInfo, Uint64 poolCount) const {
    if (poolInfo) {
        for (Uint64 i = 0; i < poolCount; i++) {
            delete[] poolInfo[i].blocks;
        }
        delete[] poolInfo;
    }
}

SmallMemoryAllocator::AllocationStats* SmallMemoryAllocator::getAllocationStats(Uint64* outStatsCount) const {
    SDL_LockMutex(mutex_);

    // First pass: count unique allocation IDs
    Uint64 maxStats = 256; // Initial capacity
    AllocationStats* tempStats = new AllocationStats[maxStats];
    Uint64 statsCount = 0;

    // Iterate through all pools and blocks to collect stats
    MemoryPool* pool = firstPool_;
    while (pool) {
        BlockHeader* block = pool->firstBlock;
        while (block) {
            if (!block->isFree && block->allocationId != nullptr) {
                // Find or create entry for this allocation ID
                Uint64 idx = 0;
                for (idx = 0; idx < statsCount; idx++) {
                    if (tempStats[idx].allocationId == block->allocationId) {
                        break;
                    }
                }

                if (idx == statsCount) {
                    // New allocation ID
                    if (statsCount >= maxStats) {
                        // Need to grow the array
                        Uint64 newMaxStats = maxStats * 2;
                        AllocationStats* newTempStats = new AllocationStats[newMaxStats];
                        SDL_memcpy(newTempStats, tempStats, statsCount * sizeof(AllocationStats));
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
            block = block->next;
        }
        pool = pool->next;
    }

    // Create right-sized output array
    AllocationStats* stats = nullptr;
    if (statsCount > 0) {
        stats = new AllocationStats[statsCount];
        SDL_memcpy(stats, tempStats, statsCount * sizeof(AllocationStats));
    }
    delete[] tempStats;

    *outStatsCount = statsCount;
    SDL_UnlockMutex(mutex_);
    return stats;
}

void SmallMemoryAllocator::freeAllocationStats(AllocationStats* stats, Uint64 statsCount) const {
    (void)statsCount; // Unused
    delete[] stats;
}

void SmallMemoryAllocator::getUsageHistory(Uint64* outHistory, Uint64* outCount) const {
    SDL_LockMutex(mutex_);

    *outCount = historyCount_;
    if (historyCount_ > 0) {
        // Copy history in chronological order
        Uint64 count = historyCount_ < HISTORY_SIZE ? historyCount_ : HISTORY_SIZE;
        if (historyCount_ < HISTORY_SIZE) {
            // Haven't wrapped around yet
            SDL_memcpy(outHistory, usageHistory_, count * sizeof(Uint64));
        } else {
            // Wrapped around - need to copy in two parts
            Uint64 firstPart = HISTORY_SIZE - historyIndex_;
            SDL_memcpy(outHistory, &usageHistory_[historyIndex_], firstPart * sizeof(Uint64));
            if (historyIndex_ > 0) {
                SDL_memcpy(&outHistory[firstPart], usageHistory_, historyIndex_ * sizeof(Uint64));
            }
        }
    }

    SDL_UnlockMutex(mutex_);
}

void SmallMemoryAllocator::updateMemoryHistory(float currentTime) {
    SDL_LockMutex(mutex_);

    // Only sample if enough time has passed
    if (currentTime - lastSampleTime_ < SAMPLE_INTERVAL) {
        SDL_UnlockMutex(mutex_);
        return;
    }

    lastSampleTime_ = currentTime;

    // Calculate used memory while already holding the lock
    Uint64 used = calculateUsedMemoryLocked();

    usageHistory_[historyIndex_] = used;
    historyIndex_ = (historyIndex_ + 1) % HISTORY_SIZE;
    if (historyCount_ < HISTORY_SIZE) {
        historyCount_++;
    }

    SDL_UnlockMutex(mutex_);
}

Uint64 SmallMemoryAllocator::calculateUsedMemoryLocked() const {
    Uint64 used = 0;
    MemoryPool* pool = firstPool_;
    while (pool) {
        // Iterate through all blocks in this pool
        BlockHeader* block = pool->firstBlock;
        while (block) {
            if (!block->isFree) {
                // Count allocated block size plus header
                used += block->size + sizeof(BlockHeader);
            }
            block = block->next;
        }
        pool = pool->next;
    }
    return used;
}

Uint64 SmallMemoryAllocator::getBlockHeaderSize() {
    return sizeof(BlockHeader);
}
#endif


