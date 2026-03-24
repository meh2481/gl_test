#include "FastMemset.h"

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#define FAST_MEMSET_X86 1
#include <immintrin.h>
#else
#define FAST_MEMSET_X86 0
#endif

#if defined(__GNUC__) || defined(__clang__)
#define FAST_MEMSET_TARGET_AVX2 __attribute__((target("avx2")))
#define FAST_MEMSET_TARGET_SSE2 __attribute__((target("sse2")))
#else
#define FAST_MEMSET_TARGET_AVX2
#define FAST_MEMSET_TARGET_SSE2
#endif

namespace {

using FastMemsetImpl = void* (*)(void*, uint8_t, uint64_t);

constexpr uint64_t SCALAR_UNROLL_BYTES = sizeof(uint64_t);
constexpr uint64_t SSE2_BLOCK_BYTES = 64;
constexpr uint64_t AVX2_BLOCK_BYTES = 128;
constexpr uint64_t STREAMING_THRESHOLD = 16 * 1024;

inline uint64_t makeRepeatedBytePattern(uint8_t value) {
    return 0x0101010101010101ULL * static_cast<uint64_t>(value);
}

inline void* scalarMemsetImpl(void* destination, uint8_t value, uint64_t size) {
    uint8_t* out = static_cast<uint8_t*>(destination);

    while (size > 0 && (reinterpret_cast<uintptr_t>(out) & (SCALAR_UNROLL_BYTES - 1)) != 0) {
        *out++ = value;
        --size;
    }

    const uint64_t pattern = makeRepeatedBytePattern(value);
    uint64_t* out64 = reinterpret_cast<uint64_t*>(out);
    while (size >= SCALAR_UNROLL_BYTES) {
        *out64++ = pattern;
        size -= SCALAR_UNROLL_BYTES;
    }

    out = reinterpret_cast<uint8_t*>(out64);
    while (size > 0) {
        *out++ = value;
        --size;
    }

    return destination;
}

#if FAST_MEMSET_X86
FAST_MEMSET_TARGET_SSE2
void* sse2MemsetImpl(void* destination, uint8_t value, uint64_t size) {
    uint8_t* out = static_cast<uint8_t*>(destination);
    if (size < SSE2_BLOCK_BYTES) {
        return scalarMemsetImpl(destination, value, size);
    }

    const __m128i fill = _mm_set1_epi8(static_cast<char>(value));

    while (size > 0 && (reinterpret_cast<uintptr_t>(out) & 15U) != 0) {
        *out++ = value;
        --size;
    }

    while (size >= SSE2_BLOCK_BYTES) {
        _mm_store_si128(reinterpret_cast<__m128i*>(out + 0), fill);
        _mm_store_si128(reinterpret_cast<__m128i*>(out + 16), fill);
        _mm_store_si128(reinterpret_cast<__m128i*>(out + 32), fill);
        _mm_store_si128(reinterpret_cast<__m128i*>(out + 48), fill);
        out += SSE2_BLOCK_BYTES;
        size -= SSE2_BLOCK_BYTES;
    }

    while (size >= 16) {
        _mm_store_si128(reinterpret_cast<__m128i*>(out), fill);
        out += 16;
        size -= 16;
    }

    return scalarMemsetImpl(out, value, size);
}

FAST_MEMSET_TARGET_AVX2
void* avx2MemsetImpl(void* destination, uint8_t value, uint64_t size) {
    uint8_t* out = static_cast<uint8_t*>(destination);
    if (size < AVX2_BLOCK_BYTES) {
        return sse2MemsetImpl(destination, value, size);
    }

    const __m256i fill = _mm256_set1_epi8(static_cast<char>(value));

    while (size > 0 && (reinterpret_cast<uintptr_t>(out) & 31U) != 0) {
        *out++ = value;
        --size;
    }

    bool usedStreamingStores = false;
    while (size >= STREAMING_THRESHOLD) {
        _mm256_stream_si256(reinterpret_cast<__m256i*>(out + 0), fill);
        _mm256_stream_si256(reinterpret_cast<__m256i*>(out + 32), fill);
        _mm256_stream_si256(reinterpret_cast<__m256i*>(out + 64), fill);
        _mm256_stream_si256(reinterpret_cast<__m256i*>(out + 96), fill);
        out += AVX2_BLOCK_BYTES;
        size -= AVX2_BLOCK_BYTES;
        usedStreamingStores = true;
    }

    while (size >= AVX2_BLOCK_BYTES) {
        _mm256_store_si256(reinterpret_cast<__m256i*>(out + 0), fill);
        _mm256_store_si256(reinterpret_cast<__m256i*>(out + 32), fill);
        _mm256_store_si256(reinterpret_cast<__m256i*>(out + 64), fill);
        _mm256_store_si256(reinterpret_cast<__m256i*>(out + 96), fill);
        out += AVX2_BLOCK_BYTES;
        size -= AVX2_BLOCK_BYTES;
    }

    while (size >= 32) {
        _mm256_store_si256(reinterpret_cast<__m256i*>(out), fill);
        out += 32;
        size -= 32;
    }

    if (usedStreamingStores) {
        _mm_sfence();
    }

    return scalarMemsetImpl(out, value, size);
}
#endif

FastMemsetImpl resolveFastMemsetImpl() {
#if FAST_MEMSET_X86 && (defined(__GNUC__) || defined(__clang__))
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx2")) {
        return &avx2MemsetImpl;
    }
    if (__builtin_cpu_supports("sse2")) {
        return &sse2MemsetImpl;
    }
#elif FAST_MEMSET_X86 && defined(__x86_64__)
    return &sse2MemsetImpl;
#endif

    return &scalarMemsetImpl;
}

} // namespace

void* fastMemset(void* destination, uint8_t value, uint64_t size) {
    if (!destination || size == 0) {
        return destination;
    }

    static FastMemsetImpl impl = resolveFastMemsetImpl();
    return impl(destination, value, size);
}