#pragma once

#include "MemoryAllocator.h"
#include <cstdint>
#include <SDL3/SDL.h>

class LargeMemoryAllocator : public MemoryAllocator {
public:
    LargeMemoryAllocator();
    ~LargeMemoryAllocator() override;

    void* allocate(uint64_t size, const char* allocationId) override;
    void free(void* ptr) override;
    uint64_t defragment() override;

    uint64_t getTotalMemory() const override;
    uint64_t getUsedMemory() const override;
    uint64_t getFreeMemory() const override;

#ifdef DEBUG
    // Debug visualization helpers
    struct ChunkInfo;
    struct BlockInfo {
        uint64_t offset;      // Offset from chunk start
        uint64_t size;        // Size of block
        bool isFree;        // Is this block free?
        const char* allocationId; // Identifier for tracking allocation source
    };

    struct ChunkInfo {
        uint64_t size;
        BlockInfo* blocks;
        uint64_t blockCount;
    };

    // Allocation statistics by ID
    struct AllocationStats {
        const char* allocationId;
        uint64_t count;
        uint64_t totalBytes;
    };

    // Get chunk information for visualization (caller must delete[] returned array)
    ChunkInfo* getChunkInfo(uint64_t* outChunkCount) const;
    void freeChunkInfo(ChunkInfo* chunkInfo, uint64_t chunkCount) const;

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
    struct MemoryChunk;

    struct alignas(16) BlockHeader {
        uint64_t size;
        bool isFree;
        BlockHeader* next;
        BlockHeader* prev;
        MemoryChunk* chunk;
        const char* allocationId;
    };

    struct MemoryChunk {
        char* memory;
        uint64_t size;
        MemoryChunk* next;
        BlockHeader* firstBlock;
    };

    MemoryChunk* m_chunks;
    uint64_t m_chunkSize;
    uint64_t m_totalPoolSize;
    uint64_t m_usedMemory;
    uint64_t m_allocationCount;
    BlockHeader* m_freeList;
    SDL_Mutex* m_mutex;

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

    void addChunk(uint64_t size);
    void removeEmptyChunks();
    BlockHeader* findFreeBlock(uint64_t size);
    void splitBlock(BlockHeader* block, uint64_t size);
    BlockHeader* mergeAdjacentBlocks(BlockHeader* block);
    MemoryChunk* findChunkForPointer(void* ptr) const;
};
