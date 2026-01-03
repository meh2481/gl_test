#pragma once

#include "MemoryAllocator.h"
#include <cstddef>
#include <cstdint>
#include <SDL3/SDL.h>

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
    void* allocate(size_t size, const char* allocationId) override;

    // Free previously allocated memory
    void free(void* ptr) override;

    // Defragment the allocator (coalesces adjacent free blocks)
    // Returns number of blocks coalesced
    size_t defragment() override;

    // Get statistics
    size_t getTotalMemory() const override;
    size_t getUsedMemory() const override;
    size_t getFreeMemory() const override;
    size_t getAllocationCount() const;

#ifdef DEBUG
    // Debug visualization helpers
    struct MemoryPoolInfo;
    struct BlockInfo {
        size_t offset;      // Offset from pool start
        size_t size;        // Size of block
        bool isFree;        // Is this block free?
        const char* allocationId; // Identifier for tracking allocation source
    };

    struct MemoryPoolInfo {
        size_t capacity;
        size_t used;
        size_t allocCount;
        BlockInfo* blocks;
        size_t blockCount;
    };

    // Allocation statistics by ID
    struct AllocationStats {
        const char* allocationId;
        size_t count;
        size_t totalBytes;
    };

    // Get pool information for visualization (caller must delete[] returned array)
    MemoryPoolInfo* getPoolInfo(size_t* outPoolCount) const;
    void freePoolInfo(MemoryPoolInfo* poolInfo, size_t poolCount) const;

    // Get allocation statistics grouped by ID (caller must delete[] returned array)
    AllocationStats* getAllocationStats(size_t* outStatsCount) const;
    void freeAllocationStats(AllocationStats* stats, size_t statsCount) const;

    // Get memory usage history
    void getUsageHistory(size_t* outHistory, size_t* outCount) const;

    // Get block header size
    static size_t getBlockHeaderSize();

    // Update memory usage history (call periodically, e.g., once per frame)
    void updateMemoryHistory(float currentTime);
#endif

private:
    // Memory pool structure - each pool is independent
    struct MemoryPool;

    // Block header stored before each allocation
    struct BlockHeader {
        size_t size;           // Size of the allocation (not including header)
        bool isFree;           // Is this block free?
        BlockHeader* next;     // Next block in the list
        BlockHeader* prev;     // Previous block in the list
        MemoryPool* pool;      // Pool this block belongs to
        const char* allocationId; // Identifier for tracking allocation source
    };

    // Memory pool structure - each pool is independent
    struct MemoryPool {
        char* memory;          // Pool memory
        size_t capacity;       // Pool capacity
        size_t used;           // Bytes used in pool
        BlockHeader* firstBlock; // First block in this pool
        BlockHeader* lastBlock;  // Last block in this pool
        size_t allocCount;     // Number of active allocations in this pool
        MemoryPool* next;      // Next pool in the list
    };

    // Pool list
    MemoryPool* firstPool_;
    MemoryPool* lastPool_;

    // Statistics
    size_t allocationCount_;
    size_t totalCapacity_;

    // Thread safety
    SDL_Mutex* mutex_;

#ifdef DEBUG
    // Memory usage history (circular buffer)
    static const size_t HISTORY_SIZE = 100;
    size_t usageHistory_[HISTORY_SIZE];
    size_t historyIndex_;
    size_t historyCount_;
    float lastSampleTime_;

    static constexpr float SAMPLE_INTERVAL = 0.1f; // Sample every 100ms
#endif

    // Minimum pool size (64KB)
    static const size_t MIN_POOL_SIZE = 64 * 1024;

    // Create a new pool with given capacity
    MemoryPool* createPool(size_t capacity);

    // Remove empty pools
    void removeEmptyPools();

    // Merge adjacent free blocks within a pool
    void coalescePool(MemoryPool* pool);

    // Find a free block that fits size across all pools
    BlockHeader* findFreeBlock(size_t size);

    // Split a block if it's larger than needed
    void splitBlock(BlockHeader* block, size_t size);
};
