#pragma once

#include <cstddef>

class MemoryAllocator {
public:
    virtual ~MemoryAllocator() = default;

    virtual void* allocate(size_t size, const char* allocationId) = 0;
    virtual void free(void* ptr) = 0;
    virtual size_t defragment() = 0;

    virtual size_t getTotalMemory() const = 0;
    virtual size_t getUsedMemory() const = 0;
    virtual size_t getFreeMemory() const = 0;
};
