#include "FastMemcpy.h"

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#define FAST_MEMCPY_X86 1
#include <immintrin.h>
#else
#define FAST_MEMCPY_X86 0
#endif

#if defined(__GNUC__) || defined(__clang__)
#define FAST_MEMCPY_TARGET_AVX2 __attribute__((target("avx2")))
#define FAST_MEMCPY_TARGET_SSE2 __attribute__((target("sse2")))
#else
#define FAST_MEMCPY_TARGET_AVX2
#define FAST_MEMCPY_TARGET_SSE2
#endif

namespace {

using FastMemcpyImpl = void* (*)(void*, const void*, size_t);

constexpr size_t SCALAR_UNROLL_BYTES = sizeof(uint64_t);
constexpr size_t SSE2_BLOCK_BYTES = 64;
constexpr size_t AVX2_BLOCK_BYTES = 128;
constexpr size_t STREAMING_THRESHOLD = 16 * 1024;

inline void* scalarMemcpyImpl(void* destination, const void* source, size_t size) {
    uint8_t* out = static_cast<uint8_t*>(destination);
    const uint8_t* in = static_cast<const uint8_t*>(source);

    // Align destination to 8-byte boundary
    while (size > 0 && (reinterpret_cast<uintptr_t>(out) & (SCALAR_UNROLL_BYTES - 1)) != 0) {
        *out++ = *in++;
        --size;
    }

    // 8-byte aligned copy
    uint64_t* out64 = reinterpret_cast<uint64_t*>(out);
    const uint64_t* in64 = reinterpret_cast<const uint64_t*>(in);
    while (size >= SCALAR_UNROLL_BYTES) {
        *out64++ = *in64++;
        size -= SCALAR_UNROLL_BYTES;
    }

    // Tail bytes
    out = reinterpret_cast<uint8_t*>(out64);
    in = reinterpret_cast<const uint8_t*>(in64);
    while (size > 0) {
        *out++ = *in++;
        --size;
    }

    return destination;
}

#if FAST_MEMCPY_X86
FAST_MEMCPY_TARGET_SSE2
void* sse2MemcpyImpl(void* destination, const void* source, size_t size) {
    uint8_t* out = static_cast<uint8_t*>(destination);
    const uint8_t* in = static_cast<const uint8_t*>(source);

    if (size < SSE2_BLOCK_BYTES) {
        return scalarMemcpyImpl(destination, source, size);
    }

    // Align both source and destination to 16 bytes if possible
    while (size > 0 && (reinterpret_cast<uintptr_t>(out) & 15U) != 0) {
        *out++ = *in++;
        --size;
    }

    // If source is also 16-byte aligned, use aligned loads/stores
    if ((reinterpret_cast<uintptr_t>(in) & 15U) == 0) {
        while (size >= SSE2_BLOCK_BYTES) {
            __m128i v0 = _mm_load_si128(reinterpret_cast<const __m128i*>(in + 0));
            __m128i v1 = _mm_load_si128(reinterpret_cast<const __m128i*>(in + 16));
            __m128i v2 = _mm_load_si128(reinterpret_cast<const __m128i*>(in + 32));
            __m128i v3 = _mm_load_si128(reinterpret_cast<const __m128i*>(in + 48));
            _mm_store_si128(reinterpret_cast<__m128i*>(out + 0), v0);
            _mm_store_si128(reinterpret_cast<__m128i*>(out + 16), v1);
            _mm_store_si128(reinterpret_cast<__m128i*>(out + 32), v2);
            _mm_store_si128(reinterpret_cast<__m128i*>(out + 48), v3);
            out += SSE2_BLOCK_BYTES;
            in += SSE2_BLOCK_BYTES;
            size -= SSE2_BLOCK_BYTES;
        }
    } else {
        // Use unaligned loads for misaligned source
        while (size >= SSE2_BLOCK_BYTES) {
            __m128i v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + 0));
            __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + 16));
            __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + 32));
            __m128i v3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + 48));
            _mm_store_si128(reinterpret_cast<__m128i*>(out + 0), v0);
            _mm_store_si128(reinterpret_cast<__m128i*>(out + 16), v1);
            _mm_store_si128(reinterpret_cast<__m128i*>(out + 32), v2);
            _mm_store_si128(reinterpret_cast<__m128i*>(out + 48), v3);
            out += SSE2_BLOCK_BYTES;
            in += SSE2_BLOCK_BYTES;
            size -= SSE2_BLOCK_BYTES;
        }
    }

    while (size >= 16) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in));
        _mm_store_si128(reinterpret_cast<__m128i*>(out), v);
        out += 16;
        in += 16;
        size -= 16;
    }

    return scalarMemcpyImpl(out, in, size);
}

FAST_MEMCPY_TARGET_AVX2
void* avx2MemcpyImpl(void* destination, const void* source, size_t size) {
    uint8_t* out = static_cast<uint8_t*>(destination);
    const uint8_t* in = static_cast<const uint8_t*>(source);

    if (size < AVX2_BLOCK_BYTES) {
        return sse2MemcpyImpl(destination, source, size);
    }

    // Align destination to 32 bytes
    while (size > 0 && (reinterpret_cast<uintptr_t>(out) & 31U) != 0) {
        *out++ = *in++;
        --size;
    }

    // Check if source is also 32-byte aligned
    bool sourceAligned = (reinterpret_cast<uintptr_t>(in) & 31U) == 0;

    // Use streaming stores for large copies
    bool usedStreamingStores = false;
    if (sourceAligned) {
        while (size >= STREAMING_THRESHOLD) {
            __m256i v0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(in + 0));
            __m256i v1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(in + 32));
            __m256i v2 = _mm256_load_si256(reinterpret_cast<const __m256i*>(in + 64));
            __m256i v3 = _mm256_load_si256(reinterpret_cast<const __m256i*>(in + 96));
            _mm256_stream_si256(reinterpret_cast<__m256i*>(out + 0), v0);
            _mm256_stream_si256(reinterpret_cast<__m256i*>(out + 32), v1);
            _mm256_stream_si256(reinterpret_cast<__m256i*>(out + 64), v2);
            _mm256_stream_si256(reinterpret_cast<__m256i*>(out + 96), v3);
            out += AVX2_BLOCK_BYTES;
            in += AVX2_BLOCK_BYTES;
            size -= AVX2_BLOCK_BYTES;
            usedStreamingStores = true;
        }
    } else {
        while (size >= STREAMING_THRESHOLD) {
            __m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + 0));
            __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + 32));
            __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + 64));
            __m256i v3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + 96));
            _mm256_stream_si256(reinterpret_cast<__m256i*>(out + 0), v0);
            _mm256_stream_si256(reinterpret_cast<__m256i*>(out + 32), v1);
            _mm256_stream_si256(reinterpret_cast<__m256i*>(out + 64), v2);
            _mm256_stream_si256(reinterpret_cast<__m256i*>(out + 96), v3);
            out += AVX2_BLOCK_BYTES;
            in += AVX2_BLOCK_BYTES;
            size -= AVX2_BLOCK_BYTES;
            usedStreamingStores = true;
        }
    }

    // Regular stores for remainder
    if (sourceAligned) {
        while (size >= AVX2_BLOCK_BYTES) {
            __m256i v0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(in + 0));
            __m256i v1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(in + 32));
            __m256i v2 = _mm256_load_si256(reinterpret_cast<const __m256i*>(in + 64));
            __m256i v3 = _mm256_load_si256(reinterpret_cast<const __m256i*>(in + 96));
            _mm256_store_si256(reinterpret_cast<__m256i*>(out + 0), v0);
            _mm256_store_si256(reinterpret_cast<__m256i*>(out + 32), v1);
            _mm256_store_si256(reinterpret_cast<__m256i*>(out + 64), v2);
            _mm256_store_si256(reinterpret_cast<__m256i*>(out + 96), v3);
            out += AVX2_BLOCK_BYTES;
            in += AVX2_BLOCK_BYTES;
            size -= AVX2_BLOCK_BYTES;
        }
    } else {
        while (size >= AVX2_BLOCK_BYTES) {
            __m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + 0));
            __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + 32));
            __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + 64));
            __m256i v3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + 96));
            _mm256_store_si256(reinterpret_cast<__m256i*>(out + 0), v0);
            _mm256_store_si256(reinterpret_cast<__m256i*>(out + 32), v1);
            _mm256_store_si256(reinterpret_cast<__m256i*>(out + 64), v2);
            _mm256_store_si256(reinterpret_cast<__m256i*>(out + 96), v3);
            out += AVX2_BLOCK_BYTES;
            in += AVX2_BLOCK_BYTES;
            size -= AVX2_BLOCK_BYTES;
        }
    }

    while (size >= 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in));
        _mm256_store_si256(reinterpret_cast<__m256i*>(out), v);
        out += 32;
        in += 32;
        size -= 32;
    }

    if (usedStreamingStores) {
        _mm_sfence();
    }

    return scalarMemcpyImpl(out, in, size);
}
#endif

FastMemcpyImpl resolveFastMemcpyImpl() {
#if FAST_MEMCPY_X86 && (defined(__GNUC__) || defined(__clang__))
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx2")) {
        return &avx2MemcpyImpl;
    }
    if (__builtin_cpu_supports("sse2")) {
        return &sse2MemcpyImpl;
    }
#elif FAST_MEMCPY_X86 && defined(__x86_64__)
    return &sse2MemcpyImpl;
#endif

    return &scalarMemcpyImpl;
}

} // namespace

void* fastMemcpy(void* destination, const void* source, size_t size) {
    if (!destination || !source || size == 0) {
        return destination;
    }

    static FastMemcpyImpl impl = resolveFastMemcpyImpl();
    return impl(destination, source, size);
}
