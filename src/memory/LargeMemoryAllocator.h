#pragma once

#include "MemoryAllocator.h"
#include <cstddef>
#include <cstdint>
#include <SDL3/SDL.h>

class LargeMemoryAllocator : public MemoryAllocator {
public:
    LargeMemoryAllocator(size_t initialChunkSize = 1024 * 1024);
    ~LargeMemoryAllocator() override;

    void* allocate(size_t size) override;
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
#endif

private:
    struct MemoryChunk;

    struct BlockHeader {
        size_t size;
        bool isFree;
        BlockHeader* next;
        BlockHeader* prev;
        MemoryChunk* chunk;
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
    BlockHeader* m_freeList;
    SDL_Mutex* m_mutex;

#ifdef DEBUG
    // Memory usage history (circular buffer)
    static const size_t HISTORY_SIZE = 100;
    size_t usageHistory_[HISTORY_SIZE];
    size_t historyIndex_;
    size_t historyCount_;

    void recordMemoryUsage();
#endif

    void addChunk(size_t size);
    void removeEmptyChunks();
    BlockHeader* findFreeBlock(size_t size);
    void splitBlock(BlockHeader* block, size_t size);
    void mergeAdjacentBlocks(BlockHeader* block);
    MemoryChunk* findChunkForPointer(void* ptr) const;
};
