#include "SmallAllocator.h"
#include <cstring>
#include <cassert>
#include <iostream>

SmallAllocator::SmallAllocator()
    : pool_(nullptr)
    , poolCapacity_(0)
    , poolUsed_(0)
    , firstBlock_(nullptr)
    , lastBlock_(nullptr)
    , allocationCount_(0)
{
    mutex_ = SDL_CreateMutex();
    assert(mutex_ != nullptr);
    // cerr instead of cout here to avoid race condition
    std::cerr << "SmallAllocator: Initializing with " << MIN_POOL_SIZE << " bytes" << std::endl;
    growPool(MIN_POOL_SIZE);
}

SmallAllocator::~SmallAllocator() {
    if (pool_) {
        // cerr instead of cout here to avoid race condition
        std::cerr << "SmallAllocator: Destroying allocator with " << allocationCount_
                  << " leaked allocations" << std::endl;
        if (allocationCount_ > 0) {
            BlockHeader* current = firstBlock_;
            while (current) {
                if (!current->isFree) {
                    std::cerr << "Leaked block: size=" << current->size << ", contents=\"" << (char*)((char*)current + sizeof(BlockHeader)) << "\"" << std::endl;
                }
                current = current->next;
            }
        }
        assert(allocationCount_ == 0);
        ::free(pool_);
        pool_ = nullptr;
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

    // Try to find a free block
    BlockHeader* block = findFreeBlock(alignedSize);

    if (!block) {
        // Need to grow the pool
        size_t neededSize = poolCapacity_ + sizeof(BlockHeader) + alignedSize;
        size_t newCapacity = poolCapacity_;
        if (newCapacity == 0) newCapacity = MIN_POOL_SIZE;

        // Grow by powers of 2
        while (newCapacity < neededSize) {
            newCapacity *= 2;
            assert(newCapacity > 0);
        }

        std::cerr << "SmallAllocator: Growing pool from " << poolCapacity_
                  << " to " << newCapacity << " bytes" << std::endl;
        growPool(newCapacity);

        // Try again after growing
        block = findFreeBlock(alignedSize);
        assert(block != nullptr);
    }

    // Mark block as used
    block->isFree = false;
    allocationCount_++;

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

    // Mark as free
    block->isFree = true;
    allocationCount_--;

    // Coalesce adjacent free blocks
    coalesce();

    // Try to shrink pool if too much is free
    if (poolCapacity_ > MIN_POOL_SIZE && poolUsed_ < poolCapacity_ / 4) {
        std::cerr << "SmallAllocator: Attempting to shrink pool (capacity=" << poolCapacity_
                  << ", used=" << poolUsed_ << ")" << std::endl;
        shrinkPool();
    }

    SDL_UnlockMutex(mutex_);
}

size_t SmallAllocator::defragment() {
    SDL_LockMutex(mutex_);

    // For a general-purpose allocator, we can't move active allocations
    // as it would invalidate user pointers. Instead, we just coalesce
    // adjacent free blocks to reduce fragmentation.
    size_t coalescedBlocks = 0;

    if (!firstBlock_) {
        SDL_UnlockMutex(mutex_);
        return 0;
    }

    BlockHeader* current = firstBlock_;

    while (current && current->next) {
        // If this block and next are both free, merge them
        if (current->isFree && current->next->isFree) {
            BlockHeader* next = current->next;

            // Expand current block
            current->size += sizeof(BlockHeader) + next->size;
            current->next = next->next;

            if (next->next) {
                next->next->prev = current;
            }

            if (next == lastBlock_) {
                lastBlock_ = current;
            }

            poolUsed_ -= sizeof(BlockHeader);
            coalescedBlocks++;
        } else {
            current = current->next;
        }
    }

    SDL_UnlockMutex(mutex_);
    return coalescedBlocks;
}

size_t SmallAllocator::getTotalMemory() const {
    SDL_LockMutex(mutex_);
    size_t result = poolCapacity_;
    SDL_UnlockMutex(mutex_);
    return result;
}

size_t SmallAllocator::getUsedMemory() const {
    SDL_LockMutex(mutex_);
    size_t result = poolUsed_;
    SDL_UnlockMutex(mutex_);
    return result;
}

size_t SmallAllocator::getFreeMemory() const {
    SDL_LockMutex(mutex_);
    size_t result = poolUsed_ <= poolCapacity_ ? poolCapacity_ - poolUsed_ : 0;
    SDL_UnlockMutex(mutex_);
    return result;
}

size_t SmallAllocator::getAllocationCount() const {
    SDL_LockMutex(mutex_);
    size_t result = allocationCount_;
    SDL_UnlockMutex(mutex_);
    return result;
}

void SmallAllocator::growPool(size_t newCapacity) {
    assert(newCapacity > poolCapacity_);

    char* newPool = (char*)::malloc(newCapacity);
    assert(newPool != nullptr);

    size_t oldCapacity = poolCapacity_;

    if (pool_) {
        // Copy existing data
        memcpy(newPool, pool_, poolUsed_);

        // Update block pointers
        ptrdiff_t offset = newPool - pool_;
        if (firstBlock_) {
            firstBlock_ = (BlockHeader*)((char*)firstBlock_ + offset);
        }
        if (lastBlock_) {
            lastBlock_ = (BlockHeader*)((char*)lastBlock_ + offset);
        }

        // Update all block pointers in the list
        BlockHeader* current = firstBlock_;
        while (current) {
            if (current->next) {
                current->next = (BlockHeader*)((char*)current->next + offset);
            }
            if (current->prev) {
                current->prev = (BlockHeader*)((char*)current->prev + offset);
            }
            current = current->next;
        }

        ::free(pool_);
    }

    pool_ = newPool;
    poolCapacity_ = newCapacity;

    // If this is the first allocation, create initial free block
    if (!firstBlock_) {
        firstBlock_ = (BlockHeader*)pool_;
        firstBlock_->size = poolCapacity_ - sizeof(BlockHeader);
        firstBlock_->isFree = true;
        firstBlock_->next = nullptr;
        firstBlock_->prev = nullptr;
        lastBlock_ = firstBlock_;
        poolUsed_ = poolCapacity_;
    } else {
        // Extend the last block or create a new free block
        size_t additionalSpace = newCapacity - oldCapacity;
        if (lastBlock_ && lastBlock_->isFree) {
            // Extend last free block
            lastBlock_->size += additionalSpace;
            poolUsed_ += additionalSpace;
        } else {
            // Create new free block
            BlockHeader* newBlock = (BlockHeader*)(pool_ + poolUsed_);
            newBlock->size = additionalSpace - sizeof(BlockHeader);
            newBlock->isFree = true;
            newBlock->next = nullptr;
            newBlock->prev = lastBlock_;

            if (lastBlock_) {
                lastBlock_->next = newBlock;
            }
            lastBlock_ = newBlock;

            if (!firstBlock_) {
                firstBlock_ = newBlock;
            }

            poolUsed_ += additionalSpace;
        }
    }
}

void SmallAllocator::shrinkPool() {
    // Coalesce free blocks first
    defragment();

    // Calculate actual used space (only allocated blocks)
    size_t actualUsed = 0;
    BlockHeader* current = firstBlock_;
    while (current) {
        if (!current->isFree) {
            actualUsed += sizeof(BlockHeader) + current->size;
        }
        current = current->next;
    }

    // Calculate new capacity (round up to power of 2)
    size_t newCapacity = MIN_POOL_SIZE;
    // Add some headroom (2x actual used) to avoid frequent reallocs
    size_t neededSize = actualUsed * 2;

    while (newCapacity < neededSize) {
        newCapacity *= 2;
    }

    // Only shrink if we can save significant space (at least 50%)
    if (newCapacity < poolCapacity_ / 2 && newCapacity < poolCapacity_) {
        // Can't shrink without moving allocations, so just coalesce for now
        // A more advanced implementation would need a compacting GC or handle system
        // For now, we rely on the natural fragmentation reduction from coalescing
    }
}

void SmallAllocator::coalesce() {
    if (!firstBlock_) return;

    BlockHeader* current = firstBlock_;

    while (current && current->next) {
        // If this block and next are both free, merge them
        if (current->isFree && current->next->isFree) {
            BlockHeader* next = current->next;

            // Expand current block
            current->size += sizeof(BlockHeader) + next->size;
            current->next = next->next;

            if (next->next) {
                next->next->prev = current;
            }

            if (next == lastBlock_) {
                lastBlock_ = current;
            }

            poolUsed_ -= sizeof(BlockHeader);
        } else {
            current = current->next;
        }
    }
}

SmallAllocator::BlockHeader* SmallAllocator::findFreeBlock(size_t size) {
    BlockHeader* current = firstBlock_;

    // First-fit strategy for better performance
    while (current) {
        if (current->isFree && current->size >= size) {
            return current;
        }
        current = current->next;
    }

    return nullptr;
}

void SmallAllocator::splitBlock(BlockHeader* block, size_t size) {
    assert(block != nullptr);
    assert(!block->isFree);
    assert(block->size >= size);

    // Only split if remaining space is worth creating a new block
    size_t remainingSize = block->size - size;
    if (remainingSize >= sizeof(BlockHeader) + 8) {
        // Create new free block from the remainder
        BlockHeader* newBlock = (BlockHeader*)((char*)block + sizeof(BlockHeader) + size);
        newBlock->size = remainingSize - sizeof(BlockHeader);
        newBlock->isFree = true;
        newBlock->next = block->next;
        newBlock->prev = block;

        if (block->next) {
            block->next->prev = newBlock;
        }
        block->next = newBlock;

        if (block == lastBlock_) {
            lastBlock_ = newBlock;
        }

        // Shrink current block
        block->size = size;
        // Note: poolUsed_ doesn't change - we're just reorganizing existing space
    }
}
