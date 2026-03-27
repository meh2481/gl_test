#pragma once

#include "MemoryAllocator.h"
#include <SDL3/SDL.h>

// Small memory allocator optimized for frequent small allocations
// - Uses pooled memory for cache-friendly access
// - Dynamically grows/shrinks by powers of 2
// - Automatic defragmentation
// - No STL dependencies

class SmallMemoryAllocator : public MemoryAllocator {
public:
    SmallMemoryAllocator();
    ~SmallMemoryAllocator() override;

    // Allocate memory of given size
    // Returns nullptr if allocation fails
    void* allocate(Uint64 size, const char* allocationId) override;

    // Free previously allocated memory
    void free(void* ptr) override;

    // Defragment the allocator (coalesces adjacent free blocks)
    // Returns number of blocks coalesced
    Uint64 defragment() override;

#ifdef DEBUG
    // Get statistics
    Uint64 getTotalMemory() const override;
    Uint64 getUsedMemory() const override;
    Uint64 getFreeMemory() const override;
    Uint64 getAllocationCount() const;

    // Debug visualization helpers
    struct MemoryPoolInfo;
    struct BlockInfo {
        Uint64 offset;      // Offset from pool start
        Uint64 size;        // Size of block
        bool isFree;        // Is this block free?
        const char* allocationId; // Identifier for tracking allocation source
    };

    struct MemoryPoolInfo {
        Uint64 capacity;
        Uint64 used;
        Uint64 allocCount;
        BlockInfo* blocks;
        Uint64 blockCount;
    };

    // Allocation statistics by ID
    struct AllocationStats {
        const char* allocationId;
        Uint64 count;
        Uint64 totalBytes;
    };

    // Get pool information for visualization (caller must delete[] returned array)
    MemoryPoolInfo* getPoolInfo(Uint64* outPoolCount) const;
    void freePoolInfo(MemoryPoolInfo* poolInfo, Uint64 poolCount) const;

    // Get allocation statistics grouped by ID (caller must delete[] returned array)
    AllocationStats* getAllocationStats(Uint64* outStatsCount) const;
    void freeAllocationStats(AllocationStats* stats, Uint64 statsCount) const;

    // Get memory usage history
    void getUsageHistory(Uint64* outHistory, Uint64* outCount) const;

    // Get block header size
    static Uint64 getBlockHeaderSize();

    // Update memory usage history (call periodically, e.g., once per frame)
    void updateMemoryHistory(float currentTime);
#endif

private:
    // Memory pool structure - each pool is independent
    struct MemoryPool;

    // Block header stored before each allocation
    struct BlockHeader {
        Uint64 size;           // Size of the allocation (not including header)
        bool isFree;           // Is this block free?
        BlockHeader* next;     // Next block in the list
        BlockHeader* prev;     // Previous block in the list
        MemoryPool* pool;      // Pool this block belongs to
        const char* allocationId; // Identifier for tracking allocation source
    };

    // Memory pool structure - each pool is independent
    struct MemoryPool {
        char* memory;          // Pool memory
        Uint64 capacity;       // Pool capacity
        Uint64 used;           // Bytes used in pool
        BlockHeader* firstBlock; // First block in this pool
        BlockHeader* lastBlock;  // Last block in this pool
        Uint64 allocCount;     // Number of active allocations in this pool
        MemoryPool* next;      // Next pool in the list
    };

    // Pool list
    MemoryPool* firstPool_;
    MemoryPool* lastPool_;

    // Statistics
    Uint64 allocationCount_;
    Uint64 totalCapacity_;

    // Thread safety
    SDL_Mutex* mutex_;

#ifdef DEBUG
    // Memory usage history (circular buffer)
    // With 0.1s sample interval and 3000 samples = 300 seconds = 5 minutes
    static const Uint64 HISTORY_SIZE = 3000;
    Uint64 usageHistory_[HISTORY_SIZE];
    Uint64 historyIndex_;
    Uint64 historyCount_;
    float lastSampleTime_;

    static constexpr float SAMPLE_INTERVAL = 0.1f; // Sample every 100ms
#endif

    // Minimum pool size (64KB)
    static const Uint64 MIN_POOL_SIZE = 64 * 1024;

    // Create a new pool with given capacity
    MemoryPool* createPool(Uint64 capacity);

    // Remove empty pools
    void removeEmptyPools();

    // Merge adjacent free blocks within a pool
    void coalescePool(MemoryPool* pool);

    // Find a free block that fits size across all pools
    BlockHeader* findFreeBlock(Uint64 size);

    // Split a block if it's larger than needed
    void splitBlock(BlockHeader* block, Uint64 size);

    // Calculate used memory without locking (caller must hold mutex_)
    Uint64 calculateUsedMemoryLocked() const;
};
