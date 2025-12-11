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

    void addChunk(size_t size);
    void removeEmptyChunks();
    BlockHeader* findFreeBlock(size_t size);
    void splitBlock(BlockHeader* block, size_t size);
    void mergeAdjacentBlocks(BlockHeader* block);
    MemoryChunk* findChunkForPointer(void* ptr) const;
};
