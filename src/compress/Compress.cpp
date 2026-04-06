// CMPR — fast lossless block compressor / decompressor.
// Optimised for decompression speed; no external dependencies.
//
// ─── Stream format ───────────────────────────────────────────────────────────
//
//   Offset  Size  Field
//   ──────  ────  ───────────────────────────────────────────────────────────
//   0       4     Magic = 0x52504D43  ('C','M','P','R' in memory, LE)
//   4       4     origSize  (uint32 little-endian)
//   8       …     Compressed sequences (see below)
//
// ─── Sequence format ─────────────────────────────────────────────────────────
//
//   Every sequence encodes a literal run followed by a match copy.
//   The final sequence in the stream has no match (literals only).
//
//     [1]  token = (litLenNib << 4) | matchLenNib
//
//     Extra literal-length bytes   (present when litLenNib == 15):
//       Zero or more bytes equal to 255, then one terminator byte < 255.
//       Total litLen = litLenNib + Σ(255 bytes) + terminator byte.
//
//     [litLen]  literal bytes
//
//     [2]  matchOffset  (uint16 LE, distance in bytes back from current
//                        output position; 1 = previous byte)
//          ← absent for the last sequence
//
//     Extra match-length bytes   (same encoding as literal extras)
//       Effective matchLen = matchLenNib + MINMATCH + extra.
//
// MINMATCH = 4  (minimum encoded back-reference length)
// Max back-reference offset = 65535 bytes
//
// ─────────────────────────────────────────────────────────────────────────────

#include "Compress.h"

namespace {

static const uint32_t MAGIC    = 0x52504D43u;  // 'CMPR'
static const uint32_t MINMATCH = 4u;
static const uint32_t HASHLOG  = 16u;
static const uint32_t HTSIZE   = 1u << HASHLOG;  // 65 536 entries

// ── Portable unaligned load / store ──────────────────────────────────────────
// __builtin_memcpy with a compile-time-constant size is *always* inlined by
// GCC and Clang — no library call is ever generated, making these helpers
// safe in -nostdlib compilation units.

static inline uint16_t load16(const uint8_t* p) {
    uint16_t v; __builtin_memcpy(&v, p, 2); return v;
}
static inline uint32_t load32(const uint8_t* p) {
    uint32_t v; __builtin_memcpy(&v, p, 4); return v;
}
static inline void store16(uint8_t* p, uint16_t v) { __builtin_memcpy(p, &v, 2); }
static inline void store32(uint8_t* p, uint32_t v) { __builtin_memcpy(p, &v, 4); }

// ── Hash ─────────────────────────────────────────────────────────────────────
// Knuth multiplicative hash maps any 4-byte value uniformly into [0, HTSIZE).

static inline uint32_t hash4(const uint8_t* p) {
    return (load32(p) * 2654435761u) >> (32u - HASHLOG);
}

// ── Variable-size copy and fill using fixed-size builtins ────────────────────
// These avoid emitting calls to memcpy / memset, which would be unresolvable
// in the -nostdlib game binary.  The inner loops use constant-size
// __builtin_memcpy calls that GCC/Clang always inline; the auto-vectoriser
// handles wider SIMD promotion from there.

// Copy exactly n bytes, non-overlapping source and destination.
static inline void copyN(uint8_t* __restrict__ d,
                         const uint8_t* __restrict__ s, size_t n) {
    while (n >= 8) { __builtin_memcpy(d, s, 8); d += 8; s += 8; n -= 8; }
    while (n--)    { *d++ = *s++; }
}

// Fill n bytes with a single repeated byte value.
static inline void fillN(uint8_t* d, uint8_t v, size_t n) {
    // Broadcast v into all 8 bytes of a uint64 for wide writes.
    const uint64_t vv = (uint64_t)v * 0x0101010101010101ULL;
    while (n >= 8) { __builtin_memcpy(d, &vv, 8); d += 8; n -= 8; }
    while (n--)    { *d++ = v; }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
namespace Compress {

// ── maxSize ───────────────────────────────────────────────────────────────────

size_t maxSize(size_t n) {
    // 8-byte header + worst-case literal overhead (1 extra byte per 255 of
    // input, for the length-extension encoding) + a small fixed margin.
    return 8 + n + (n / 255) + 16;
}

// ── compress ─────────────────────────────────────────────────────────────────

size_t compress(const void* vsrc, size_t srcLen, void* vdst, size_t dstCap) {
    if (!vsrc || !vdst || dstCap < 8) return 0;

    const uint8_t* src = static_cast<const uint8_t*>(vsrc);
    uint8_t*       dst = static_cast<uint8_t*>(vdst);

    store32(dst,     MAGIC);
    store32(dst + 4, static_cast<uint32_t>(srcLen));

    if (srcLen == 0) return 8;
    if (dstCap < maxSize(srcLen)) return 0;

    // Hash table: slot h holds the most-recent src offset whose 4-byte prefix
    // hashes to h, or 0xFFFF'FFFF when empty.
    // At HTSIZE = 65536 this is a 256 KiB stack frame — acceptable in a packer
    // / build-tool context; the game binary never calls compress().
    uint32_t ht[HTSIZE];
    for (uint32_t i = 0; i < HTSIZE; ++i) ht[i] = 0xFFFFFFFFu;

    const uint8_t* ip     = src;
    const uint8_t* anchor = src;
    const uint8_t* const srcEnd     = src + srcLen;
    // Last position from which a MINMATCH-byte match can start.
    const uint8_t* const matchLimit = srcEnd - MINMATCH;

    uint8_t* op = dst + 8;

    if (srcLen >= MINMATCH) {
        ht[hash4(ip)] = 0u;
        ++ip;

        while (ip <= matchLimit) {
            const uint32_t h       = hash4(ip);
            const uint32_t prevPos = ht[h];
            ht[h] = static_cast<uint32_t>(ip - src);

            if (prevPos != 0xFFFFFFFFu) {
                const uint8_t* matchPtr = src + prevPos;
                const uint32_t offset   = static_cast<uint32_t>(ip - matchPtr);

                if (offset > 0u && offset <= 65535u &&
                    load32(ip) == load32(matchPtr)) {

                    // Extend match as far as possible.
                    const uint8_t* mf = ip      + MINMATCH;
                    const uint8_t* rf = matchPtr + MINMATCH;
                    while (mf < srcEnd && *mf == *rf) { ++mf; ++rf; }

                    const size_t matchLen = static_cast<size_t>(mf - ip);
                    const size_t litLen   = static_cast<size_t>(ip - anchor);

                    // ── encode token ──────────────────────────────────────
                    const uint8_t litNib  = litLen >= 15u
                                          ? 15u
                                          : static_cast<uint8_t>(litLen);
                    const uint8_t mlenNib = (matchLen - MINMATCH) >= 15u
                                          ? 15u
                                          : static_cast<uint8_t>(matchLen - MINMATCH);
                    *op++ = static_cast<uint8_t>((litNib << 4) | mlenNib);

                    // ── extra literal-length bytes ────────────────────────
                    if (litLen >= 15u) {
                        size_t r = litLen - 15u;
                        while (r >= 255u) { *op++ = 255; r -= 255u; }
                        *op++ = static_cast<uint8_t>(r);
                    }

                    // ── literal bytes ─────────────────────────────────────
                    copyN(op, anchor, litLen);
                    op += litLen;

                    // ── match offset ──────────────────────────────────────
                    store16(op, static_cast<uint16_t>(offset));
                    op += 2;

                    // ── extra match-length bytes ──────────────────────────
                    const size_t extraML = matchLen - MINMATCH;
                    if (extraML >= 15u) {
                        size_t r = extraML - 15u;
                        while (r >= 255u) { *op++ = 255; r -= 255u; }
                        *op++ = static_cast<uint8_t>(r);
                    }

                    // Seed a couple of fresh hash entries from within the
                    // match to give future positions more coverage.
                    if (matchLen >= 6u) {
                        ht[hash4(ip + 2)]  = static_cast<uint32_t>(ip + 2 - src);
                        ht[hash4(mf  - 3)] = static_cast<uint32_t>(mf  - 3 - src);
                    }

                    ip     = mf;
                    anchor = ip;
                    if (ip > matchLimit) break;
                    continue;
                }
            }
            ++ip;
        }
    }

    // ── Final literal-only sequence ───────────────────────────────────────────
    // No match offset follows — the decoder exits when output is full.
    const size_t litLen  = static_cast<size_t>(srcEnd - anchor);
    const uint8_t litNib = litLen >= 15u ? 15u : static_cast<uint8_t>(litLen);
    *op++ = static_cast<uint8_t>(litNib << 4);  // lower nibble unused

    if (litLen >= 15u) {
        size_t r = litLen - 15u;
        while (r >= 255u) { *op++ = 255; r -= 255u; }
        *op++ = static_cast<uint8_t>(r);
    }

    copyN(op, anchor, litLen);
    op += litLen;

    return static_cast<size_t>(op - dst);
}

// ── decompress ───────────────────────────────────────────────────────────────
// Core design goals:
//   • Zero dynamic allocation.
//   • No implicit memcpy / memset calls (safe under -nostdlib).
//   • Match copies use a forward-copy strategy that correctly reconstructs
//     overlapping (RLE-like) back-references:
//       offset >= matchLen  → non-overlapping: copyN()
//       offset == 1         → RLE fill: fillN()
//       offset in [2, 3]    → 2/3-byte period pattern: byte-by-byte loop
//       offset in [4, …)    → 4-byte stride forward copy
//     The 4-byte stride works because after each 4-byte write the source
//     pointer advances into data we just wrote, naturally reproducing any
//     repeating pattern with period <= offset.

size_t decompress(const void* vsrc, size_t srcLen, void* vdst, size_t dstCap) {
    if (!vsrc || !vdst || srcLen < 8) return 0;

    const uint8_t* ip     = static_cast<const uint8_t*>(vsrc);
    const uint8_t* ipEnd  = ip + srcLen;
    uint8_t*       op     = static_cast<uint8_t*>(vdst);
    const uint8_t* opBase = op;

    if (load32(ip) != MAGIC) return 0;
    const uint32_t origSize = load32(ip + 4);
    ip += 8;

    if (origSize == 0) return 0;
    if (dstCap < static_cast<size_t>(origSize)) return 0;

    uint8_t* const opEnd = op + origSize;

    while (ip < ipEnd) {
        const uint8_t token = *ip++;

        // ── Literal length ────────────────────────────────────────────────────
        size_t litLen = static_cast<size_t>(token >> 4);
        if (litLen == 15u) {
            uint8_t b;
            do {
                if (ip >= ipEnd) return 0;
                b = *ip++;
                litLen += b;
            } while (b == 255);
        }

        if (static_cast<size_t>(ipEnd - ip) < litLen) return 0;
        if (static_cast<size_t>(opEnd - op) < litLen) return 0;

        // ── Copy literals ─────────────────────────────────────────────────────
        copyN(op, ip, litLen);
        ip += litLen;
        op += litLen;

        if (op == opEnd) break;  // last sequence — no match follows

        // ── Match offset ──────────────────────────────────────────────────────
        if (ip + 2 > ipEnd) return 0;
        const uint16_t offset = load16(ip);
        ip += 2;

        if (offset == 0u) return 0;

        const uint8_t* matchPtr = op - offset;
        if (matchPtr < opBase) return 0;  // back-reference out of range

        // ── Match length ──────────────────────────────────────────────────────
        size_t matchLen = static_cast<size_t>(token & 0xFu) + MINMATCH;
        if ((token & 0xFu) == 15u) {
            uint8_t b;
            do {
                if (ip >= ipEnd) return 0;
                b = *ip++;
                matchLen += b;
            } while (b == 255);
        }

        if (static_cast<size_t>(opEnd - op) < matchLen) return 0;

        // ── Copy match ────────────────────────────────────────────────────────
        // Always forward copy: correctly reconstructs repeated patterns when
        // offset < matchLen (source and destination overlap).
        uint8_t*       d   = op;
        const uint8_t* s   = matchPtr;
        const uint8_t* end = op + matchLen;

        if (static_cast<size_t>(offset) >= matchLen) {
            // Non-overlapping: source bytes are all before the destination.
            copyN(d, s, matchLen);
        } else if (offset == 1u) {
            // Period-1 RLE: fill the run with a single repeated byte.
            fillN(d, s[0], matchLen);
        } else if (offset >= 4u) {
            // Period 4..N: 4-byte-stride forward copy.
            // After the first 4-byte write the source pointer lands on data
            // we just wrote, reproducing the periodic pattern naturally.
            while (end - d >= 4) {
                __builtin_memcpy(d, s, 4);
                d += 4;
                s += 4;
            }
            while (d < end) *d++ = *s++;
        } else {
            // Period 2 or 3: byte-by-byte forward copy.
            while (d < end) *d++ = *s++;
        }

        op += matchLen;
    }

    if (op != opEnd) return 0;  // truncated or corrupt stream
    return static_cast<size_t>(origSize);
}

// ── originalSize ─────────────────────────────────────────────────────────────

uint32_t originalSize(const void* vsrc, size_t srcLen) {
    if (!vsrc || srcLen < 8) return 0u;
    const uint8_t* p = static_cast<const uint8_t*>(vsrc);
    if (load32(p) != MAGIC) return 0u;
    return load32(p + 4);
}

} // namespace Compress
