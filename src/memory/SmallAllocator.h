#pragma once

#include "MemoryAllocator.h"
#include <cstddef>
#include <cstdint>

// Small memory allocator optimized for frequent small allocations
// - Uses pooled memory for cache-friendly access
// - Dynamically grows/shrinks by powers of 2
// - Automatic defragmentation
// - No STL dependencies
class SmallAllocator : public MemoryAllocator {
public:
    SmallAllocator();
    ~SmallAllocator() override;

    // Allocate memory of given size
    // Returns nullptr if allocation fails
    void* allocate(size_t size) override;

    // Free previously allocated memory
    void free(void* ptr) override;

    // Defragment the allocator (coalesces adjacent free blocks)
    // Returns number of blocks coalesced
    size_t defragment() override;

    // Get statistics
    size_t getTotalMemory() const override { return poolCapacity_; }
    size_t getUsedMemory() const override { return poolUsed_; }
    size_t getFreeMemory() const override { return poolUsed_ <= poolCapacity_ ? poolCapacity_ - poolUsed_ : 0; }
    size_t getAllocationCount() const { return allocationCount_; }

private:
    // Block header stored before each allocation
    struct BlockHeader {
        size_t size;           // Size of the allocation (not including header)
        bool isFree;           // Is this block free?
        BlockHeader* next;     // Next block in the list
        BlockHeader* prev;     // Previous block in the list
    };

    // Memory pool
    char* pool_;
    size_t poolCapacity_;
    size_t poolUsed_;

    // Block list
    BlockHeader* firstBlock_;
    BlockHeader* lastBlock_;

    // Statistics
    size_t allocationCount_;

    // Minimum pool size (64KB)
    static const size_t MIN_POOL_SIZE = 64 * 1024;

    // Grow the pool to newCapacity
    void growPool(size_t newCapacity);

    // Shrink the pool if possible
    void shrinkPool();

    // Merge adjacent free blocks
    void coalesce();

    // Find a free block that fits size
    BlockHeader* findFreeBlock(size_t size);

    // Split a block if it's larger than needed
    void splitBlock(BlockHeader* block, size_t size);
};
