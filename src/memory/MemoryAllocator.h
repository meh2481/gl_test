#pragma once

#include <cstdint>

class MemoryAllocator {
public:
    virtual ~MemoryAllocator() = default;

    virtual void* allocate(uint64_t size, const char* allocationId) = 0;
    virtual void free(void* ptr) = 0;
    virtual uint64_t defragment() = 0;

#ifdef DEBUG
    virtual uint64_t getTotalMemory() const = 0;
    virtual uint64_t getUsedMemory() const = 0;
    virtual uint64_t getFreeMemory() const = 0;
#endif
};
