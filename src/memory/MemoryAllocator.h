#pragma once

#include <SDL3/SDL_stdinc.h>

class MemoryAllocator {
public:
    virtual ~MemoryAllocator() = default;

    virtual void* allocate(Uint64 size, const char* allocationId) = 0;
    virtual void free(void* ptr) = 0;
    virtual Uint64 defragment() = 0;

#ifdef DEBUG
    virtual Uint64 getTotalMemory() const = 0;
    virtual Uint64 getUsedMemory() const = 0;
    virtual Uint64 getFreeMemory() const = 0;
#endif
};
