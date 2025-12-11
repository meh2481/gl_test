#include "SmallAllocator.h"
#include <cstring>
#include <cassert>
#include <iostream>

SmallAllocator::SmallAllocator()
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
    memset(usageHistory_, 0, sizeof(usageHistory_));
#endif
    std::cerr << "SmallAllocator: Initializing with multi-pool architecture" << std::endl;
    // Create initial pool
    createPool(MIN_POOL_SIZE);
}

SmallAllocator::~SmallAllocator() {
    // cerr instead of cout here to avoid race condition
    std::cerr << "SmallAllocator: Destroying allocator with " << allocationCount_
              << " leaked allocations across " ;
    
    // Count pools
    size_t poolCount = 0;
    MemoryPool* pool = firstPool_;
    while (pool) {
        poolCount++;
        pool = pool->next;
    }
    std::cerr << poolCount << " pools" << std::endl;
    
    if (allocationCount_ > 0) {
        pool = firstPool_;
        while (pool) {
            BlockHeader* current = pool->firstBlock;
            while (current) {
                if (!current->isFree) {
                    std::cerr << "Leaked block: size=" << current->size << std::endl;
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
        ::free(pool->memory);
        delete pool;
        pool = next;
    }
    
    if (mutex_) {
        SDL_DestroyMutex(mutex_);
    }
}

void* SmallAllocator::allocate(size_t size) {
    SDL_LockMutex(mutex_);

    assert(size > 0);

    // Align size to 8 bytes for better cache performance
    size_t alignedSize = (size + 7) & ~7;

    // Try to find a free block in existing pools
    BlockHeader* block = findFreeBlock(alignedSize);

    if (!block) {
        // Need to create a new pool
        size_t neededSize = sizeof(BlockHeader) + alignedSize;
        size_t newPoolSize = MIN_POOL_SIZE;
        
        // If we need more than MIN_POOL_SIZE, round up to next power of 2
        while (newPoolSize < neededSize) {
            newPoolSize *= 2;
        }
        
        // Make new pool at least 2x the last pool size for exponential growth
        if (lastPool_) {
            size_t minNewSize = lastPool_->capacity * 2;
            if (newPoolSize < minNewSize) {
                newPoolSize = minNewSize;
            }
        }

        std::cerr << "SmallAllocator: Creating new pool of " << newPoolSize << " bytes" << std::endl;
        MemoryPool* newPool = createPool(newPoolSize);
        
        // Try again in the new pool
        block = findFreeBlock(alignedSize);
        assert(block != nullptr);
    }

    // Mark block as used
    block->isFree = false;
    allocationCount_++;
    block->pool->allocCount++;

    // Split block if it's much larger than needed
    splitBlock(block, alignedSize);

    // Return pointer after header
    void* ptr = (char*)block + sizeof(BlockHeader);
    SDL_UnlockMutex(mutex_);
    return ptr;
}

void SmallAllocator::free(void* ptr) {
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

#ifdef DEBUG
    recordMemoryUsage();
#endif

    SDL_UnlockMutex(mutex_);
}

size_t SmallAllocator::defragment() {
    SDL_LockMutex(mutex_);

    // Coalesce free blocks in each pool
    size_t totalCoalesced = 0;
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

size_t SmallAllocator::getTotalMemory() const {
    SDL_LockMutex(mutex_);
    size_t result = totalCapacity_;
    SDL_UnlockMutex(mutex_);
    return result;
}

size_t SmallAllocator::getUsedMemory() const {
    SDL_LockMutex(mutex_);
    size_t used = 0;
    MemoryPool* pool = firstPool_;
    while (pool) {
        used += pool->used;
        pool = pool->next;
    }
    SDL_UnlockMutex(mutex_);
    return used;
}

size_t SmallAllocator::getFreeMemory() const {
    SDL_LockMutex(mutex_);
    size_t used = 0;
    MemoryPool* pool = firstPool_;
    while (pool) {
        used += pool->used;
        pool = pool->next;
    }
    size_t result = totalCapacity_ > used ? totalCapacity_ - used : 0;
    SDL_UnlockMutex(mutex_);
    return result;
}

size_t SmallAllocator::getAllocationCount() const {
    SDL_LockMutex(mutex_);
    size_t result = allocationCount_;
    SDL_UnlockMutex(mutex_);
    return result;
}

SmallAllocator::MemoryPool* SmallAllocator::createPool(size_t capacity) {
    // Allocate pool structure
    MemoryPool* pool = new MemoryPool();
    pool->memory = (char*)::malloc(capacity);
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

void SmallAllocator::removeEmptyPools() {
    MemoryPool* pool = firstPool_;
    MemoryPool* prev = nullptr;
    
    while (pool) {
        MemoryPool* next = pool->next;
        
        // Remove pool if it has no active allocations and it's not the only pool
        if (pool->allocCount == 0 && (firstPool_ != lastPool_)) {
            std::cerr << "SmallAllocator: Removing empty pool of " << pool->capacity << " bytes" << std::endl;
            
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
            ::free(pool->memory);
            delete pool;
        } else {
            prev = pool;
        }
        
        pool = next;
    }
}

void SmallAllocator::coalescePool(MemoryPool* pool) {
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

SmallAllocator::BlockHeader* SmallAllocator::findFreeBlock(size_t size) {
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

void SmallAllocator::splitBlock(BlockHeader* block, size_t size) {
    assert(block != nullptr);
    assert(!block->isFree);
    assert(block->size >= size);
    assert(block->pool != nullptr);

    // Only split if remaining space is worth creating a new block
    size_t remainingSize = block->size - size;
    if (remainingSize >= sizeof(BlockHeader) + 8) {
        MemoryPool* pool = block->pool;
        
        // Create new free block from the remainder
        BlockHeader* newBlock = (BlockHeader*)((char*)block + sizeof(BlockHeader) + size);
        newBlock->size = remainingSize - sizeof(BlockHeader);
        newBlock->isFree = true;
        newBlock->next = block->next;
        newBlock->prev = block;
        newBlock->pool = pool;

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
SmallAllocator::MemoryPoolInfo* SmallAllocator::getPoolInfo(size_t* outPoolCount) const {
    SDL_LockMutex(mutex_);

    // Count pools
    size_t poolCount = 0;
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
    for (size_t i = 0; i < poolCount; i++) {
        assert(pool != nullptr);

        poolInfo[i].capacity = pool->capacity;
        poolInfo[i].used = pool->used;
        poolInfo[i].allocCount = pool->allocCount;

        // Count blocks in this pool
        size_t blockCount = 0;
        BlockHeader* block = pool->firstBlock;
        while (block) {
            blockCount++;
            block = block->next;
        }

        poolInfo[i].blockCount = blockCount;
        poolInfo[i].blocks = new BlockInfo[blockCount];

        // Fill in block information
        block = pool->firstBlock;
        for (size_t j = 0; j < blockCount; j++) {
            assert(block != nullptr);
            poolInfo[i].blocks[j].offset = (char*)block - pool->memory;
            poolInfo[i].blocks[j].size = block->size;
            poolInfo[i].blocks[j].isFree = block->isFree;
            block = block->next;
        }

        pool = pool->next;
    }

    SDL_UnlockMutex(mutex_);
    return poolInfo;
}

void SmallAllocator::freePoolInfo(MemoryPoolInfo* poolInfo, size_t poolCount) const {
    if (poolInfo) {
        for (size_t i = 0; i < poolCount; i++) {
            delete[] poolInfo[i].blocks;
        }
        delete[] poolInfo;
    }
}

void SmallAllocator::getUsageHistory(size_t* outHistory, size_t* outCount) const {
    SDL_LockMutex(mutex_);

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

    SDL_UnlockMutex(mutex_);
}

void SmallAllocator::recordMemoryUsage() {
    // Calculate used memory (must be called with mutex locked)
    size_t used = 0;
    MemoryPool* pool = firstPool_;
    while (pool) {
        used += pool->used;
        pool = pool->next;
    }

    usageHistory_[historyIndex_] = used;
    historyIndex_ = (historyIndex_ + 1) % HISTORY_SIZE;
    if (historyCount_ < HISTORY_SIZE) {
        historyCount_++;
    }
}

size_t SmallAllocator::getBlockHeaderSize() {
    return sizeof(BlockHeader);
}
#endif


