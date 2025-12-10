#pragma once

#include <cstddef>
#include <cstdint>

class LargeMemoryAllocator {
public:
    LargeMemoryAllocator(size_t initialChunkSize = 1024 * 1024);
    ~LargeMemoryAllocator();

    void* allocate(size_t size);
    void deallocate(void* ptr);
    void defragment();

    size_t getTotalPoolSize() const { return m_totalPoolSize; }
    size_t getUsedMemory() const { return m_usedMemory; }
    size_t getFreeMemory() const { return m_totalPoolSize - m_usedMemory; }

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

    void addChunk(size_t size);
    void removeEmptyChunks();
    BlockHeader* findFreeBlock(size_t size);
    void splitBlock(BlockHeader* block, size_t size);
    void mergeAdjacentBlocks(BlockHeader* block);
    MemoryChunk* findChunkForPointer(void* ptr) const;
};
