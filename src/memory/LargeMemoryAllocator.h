#pragma once

#include "MemoryAllocator.h"
#include <SDL3/SDL.h>

class LargeMemoryAllocator : public MemoryAllocator {
public:
    LargeMemoryAllocator();
    ~LargeMemoryAllocator() override;

    void* allocate(Uint64 size, const char* allocationId) override;
    void free(void* ptr) override;
    Uint64 defragment() override;

#ifdef DEBUG
    Uint64 getTotalMemory() const override;
    Uint64 getUsedMemory() const override;
    Uint64 getFreeMemory() const override;

    // Debug visualization helpers
    struct ChunkInfo;
    struct BlockInfo {
        Uint64 offset;      // Offset from chunk start
        Uint64 size;        // Size of block
        bool isFree;        // Is this block free?
        const char* allocationId; // Identifier for tracking allocation source
    };

    struct ChunkInfo {
        Uint64 size;
        BlockInfo* blocks;
        Uint64 blockCount;
    };

    // Allocation statistics by ID
    struct AllocationStats {
        const char* allocationId;
        Uint64 count;
        Uint64 totalBytes;
    };

    // Get chunk information for visualization (caller must delete[] returned array)
    ChunkInfo* getChunkInfo(Uint64* outChunkCount) const;
    void freeChunkInfo(ChunkInfo* chunkInfo, Uint64 chunkCount) const;

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
    struct MemoryChunk;

    struct alignas(16) BlockHeader {
        Uint64 size;
        bool isFree;
        BlockHeader* next;
        BlockHeader* prev;
        MemoryChunk* chunk;
        const char* allocationId;
    };

    struct MemoryChunk {
        char* memory;
        Uint64 size;
        MemoryChunk* next;
        BlockHeader* firstBlock;
    };

    MemoryChunk* m_chunks;
    Uint64 m_chunkSize;
    Uint64 m_totalPoolSize;
    Uint64 m_usedMemory;
    Uint64 m_allocationCount;
    BlockHeader* m_freeList;
    SDL_Mutex* m_mutex;

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

    void addChunk(Uint64 size);
    void removeEmptyChunks();
    BlockHeader* findFreeBlock(Uint64 size);
    void splitBlock(BlockHeader* block, Uint64 size);
    BlockHeader* mergeAdjacentBlocks(BlockHeader* block);
    MemoryChunk* findChunkForPointer(void* ptr) const;
};
