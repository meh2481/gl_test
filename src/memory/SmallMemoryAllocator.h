#pragma once

#include "MemoryAllocator.h"
#include <cstdint>
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
    void* allocate(uint64_t size, const char* allocationId) override;

    // Free previously allocated memory
    void free(void* ptr) override;

    // Defragment the allocator (coalesces adjacent free blocks)
    // Returns number of blocks coalesced
    uint64_t defragment() override;

    // Get statistics
    uint64_t getTotalMemory() const override;
    uint64_t getUsedMemory() const override;
    uint64_t getFreeMemory() const override;
    uint64_t getAllocationCount() const;

#ifdef DEBUG
    // Debug visualization helpers
    struct MemoryPoolInfo;
    struct BlockInfo {
        uint64_t offset;      // Offset from pool start
        uint64_t size;        // Size of block
        bool isFree;        // Is this block free?
        const char* allocationId; // Identifier for tracking allocation source
    };

    struct MemoryPoolInfo {
        uint64_t capacity;
        uint64_t used;
        uint64_t allocCount;
        BlockInfo* blocks;
        uint64_t blockCount;
    };

    // Allocation statistics by ID
    struct AllocationStats {
        const char* allocationId;
        uint64_t count;
        uint64_t totalBytes;
    };

    // Get pool information for visualization (caller must delete[] returned array)
    MemoryPoolInfo* getPoolInfo(uint64_t* outPoolCount) const;
    void freePoolInfo(MemoryPoolInfo* poolInfo, uint64_t poolCount) const;

    // Get allocation statistics grouped by ID (caller must delete[] returned array)
    AllocationStats* getAllocationStats(uint64_t* outStatsCount) const;
    void freeAllocationStats(AllocationStats* stats, uint64_t statsCount) const;

    // Get memory usage history
    void getUsageHistory(uint64_t* outHistory, uint64_t* outCount) const;

    // Get block header size
    static uint64_t getBlockHeaderSize();

    // Update memory usage history (call periodically, e.g., once per frame)
    void updateMemoryHistory(float currentTime);
#endif

private:
    // Memory pool structure - each pool is independent
    struct MemoryPool;

    // Block header stored before each allocation
    struct BlockHeader {
        uint64_t size;           // Size of the allocation (not including header)
        bool isFree;           // Is this block free?
        BlockHeader* next;     // Next block in the list
        BlockHeader* prev;     // Previous block in the list
        MemoryPool* pool;      // Pool this block belongs to
        const char* allocationId; // Identifier for tracking allocation source
    };

    // Memory pool structure - each pool is independent
    struct MemoryPool {
        char* memory;          // Pool memory
        uint64_t capacity;       // Pool capacity
        uint64_t used;           // Bytes used in pool
        BlockHeader* firstBlock; // First block in this pool
        BlockHeader* lastBlock;  // Last block in this pool
        uint64_t allocCount;     // Number of active allocations in this pool
        MemoryPool* next;      // Next pool in the list
    };

    // Pool list
    MemoryPool* firstPool_;
    MemoryPool* lastPool_;

    // Statistics
    uint64_t allocationCount_;
    uint64_t totalCapacity_;

    // Thread safety
    SDL_Mutex* mutex_;

#ifdef DEBUG
    // Memory usage history (circular buffer)
    // With 0.1s sample interval and 3000 samples = 300 seconds = 5 minutes
    static const uint64_t HISTORY_SIZE = 3000;
    uint64_t usageHistory_[HISTORY_SIZE];
    uint64_t historyIndex_;
    uint64_t historyCount_;
    float lastSampleTime_;

    static constexpr float SAMPLE_INTERVAL = 0.1f; // Sample every 100ms
#endif

    // Minimum pool size (64KB)
    static const uint64_t MIN_POOL_SIZE = 64 * 1024;

    // Create a new pool with given capacity
    MemoryPool* createPool(uint64_t capacity);

    // Remove empty pools
    void removeEmptyPools();

    // Merge adjacent free blocks within a pool
    void coalescePool(MemoryPool* pool);

    // Find a free block that fits size across all pools
    BlockHeader* findFreeBlock(uint64_t size);

    // Split a block if it's larger than needed
    void splitBlock(BlockHeader* block, uint64_t size);

    // Calculate used memory without locking (caller must hold mutex_)
    uint64_t calculateUsedMemoryLocked() const;
};
