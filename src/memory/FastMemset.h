#pragma once

#include <cstddef>
#include <cstdint>

// High-throughput memory fill with runtime SIMD dispatch on supported CPUs.
// Falls back to a scalar implementation on unsupported architectures.
void* fastMemset(void* destination, uint8_t value, size_t size);

inline void* fastZeroMem(void* destination, size_t size) {
    return fastMemset(destination, 0, size);
}