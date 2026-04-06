#pragma once
#include <stddef.h>
#include <stdint.h>

// CMPR — fast lossless block compressor optimised for decompression speed.
// No external dependencies; the decompressor performs no dynamic allocation
// and is safe to compile under -nostdlib (no implicit libc calls are emitted).
//
// Compressed streams are self-describing: an 8-byte header stores the magic
// value and the original (uncompressed) size. The algorithm is an LZ77
// hash-chain compressor with a 16-bit back-reference window, producing the
// same sequence format as LZ4 block data.

namespace Compress {

// Upper bound on the compressed output size for srcLen bytes of input.
// Pass this value (or larger) as dstCapacity to compress().
size_t maxSize(size_t srcLen);

// Compress srcLen bytes from src into dst.
// dst must point to at least maxSize(srcLen) writable bytes.
// Returns the number of bytes written to dst, or 0 on failure.
size_t compress(const void* src, size_t srcLen, void* dst, size_t dstCapacity);

// Decompress a CMPR stream from src into dst.
// Returns the number of bytes written (equal to the stored original size),
// or 0 if the stream is invalid, truncated, or dstCapacity is too small.
// No dynamic allocation is performed; safe under -nostdlib.
size_t decompress(const void* src, size_t srcLen, void* dst, size_t dstCapacity);

// Read the original (uncompressed) size stored in the stream header without
// performing any decompression. Returns 0 if the header is absent or invalid.
uint32_t originalSize(const void* src, size_t srcLen);

} // namespace Compress
