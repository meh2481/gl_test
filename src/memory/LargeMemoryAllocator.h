#pragma once

#include "MemoryAllocator.h"
#include <cstddef>
#include <cstdint>
#include <SDL3/SDL.h>

// Forward declaration
class ConsoleBuffer;

class LargeMemoryAllocator : public MemoryAllocator {
public:
    LargeMemoryAllocator(ConsoleBuffer* consoleBuffer);
    ~LargeMemoryAllocator() override;

    void* allocate(size_t size, const char* allocationId) override;
    void free(void* ptr) override;
    size_t defragment() override;

    size_t getTotalMemory() const override;
    size_t getUsedMemory() const override;
    size_t getFreeMemory() const override;

#ifdef DEBUG
    // Debug visualization helpers
    struct ChunkInfo;
    struct BlockInfo {
        size_t offset;      // Offset from chunk start
        size_t size;        // Size of block
        bool isFree;        // Is this block free?
        const char* allocationId; // Identifier for tracking allocation source
    };

    struct ChunkInfo {
        size_t size;
        BlockInfo* blocks;
        size_t blockCount;
    };

    // Get chunk information for visualization (caller must delete[] returned array)
    ChunkInfo* getChunkInfo(size_t* outChunkCount) const;
    void freeChunkInfo(ChunkInfo* chunkInfo, size_t chunkCount) const;

    // Get memory usage history
    void getUsageHistory(size_t* outHistory, size_t* outCount) const;

    // Get block header size
    static size_t getBlockHeaderSize();

    // Update memory usage history (call periodically, e.g., once per frame)
    void updateMemoryHistory(float currentTime);
#endif

private:
    struct MemoryChunk;

    struct alignas(16) BlockHeader {
        size_t size;
        bool isFree;
        BlockHeader* next;
        BlockHeader* prev;
        MemoryChunk* chunk;
        const char* allocationId;
    };

    struct MemoryChunk {
        char* memory;
        size_t size;
        MemoryChunk* next;
        BlockHeader* firstBlock;
    };

    MemoryChunk* m_chunks;
    size_t m_chunkSize;
    size_t m_totalPoolSize;
    size_t m_usedMemory;
    size_t m_allocationCount;
    BlockHeader* m_freeList;
    SDL_Mutex* m_mutex;
    ConsoleBuffer* m_consoleBuffer;

#ifdef DEBUG
    // Memory usage history (circular buffer)
    static const size_t HISTORY_SIZE = 100;
    size_t usageHistory_[HISTORY_SIZE];
    size_t historyIndex_;
    size_t historyCount_;
    float lastSampleTime_;

    static constexpr float SAMPLE_INTERVAL = 0.1f; // Sample every 100ms
#endif

    void addChunk(size_t size);
    void removeEmptyChunks();
    BlockHeader* findFreeBlock(size_t size);
    void splitBlock(BlockHeader* block, size_t size);
    BlockHeader* mergeAdjacentBlocks(BlockHeader* block);
    MemoryChunk* findChunkForPointer(void* ptr) const;
};
