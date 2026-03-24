#pragma once

#include <cstdint>

// High-throughput memory copy with runtime SIMD dispatch on supported CPUs.
// Falls back to a scalar implementation on unsupported architectures.
// Non-overlapping regions only (use memmove for overlapping).
void* fastMemcpy(void* destination, const void* source, uint64_t size);
