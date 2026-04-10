// Copyright 2009 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// ETC1 encoder/decoder.
// Derived from the Android Open Source Project implementation.

#include "etc1.h"
#include <string.h>
#include <stdint.h>

// Modifier tables from the OpenGL ES 1.0 ETC1 specification (Table 4.1).
// Rows are the 8 codewords; columns are pixel indices 0-3.
static const int kModifierTable[8][4] = {
    {  2,  8,  -2,  -8 },
    {  5, 17,  -5, -17 },
    {  9, 29,  -9, -29 },
    { 13, 42, -13, -42 },
    { 18, 60, -18, -60 },
    { 24, 80, -24, -80 },
    { 33, 106, -33, -106 },
    { 47, 183, -47, -183 },
};

static inline int clamp_byte(int x) {
    return x < 0 ? 0 : (x > 255 ? 255 : x);
}

// Expand a 4-bit color value to 8-bit by replicating the nibble.
static inline int scale4to8(int v) { return (v << 4) | v; }

// Expand a 5-bit color value to 8-bit.
static inline int scale5to8(int v) { return (v << 3) | (v >> 2); }

// Return the input-buffer byte offset for pixel at column-major output position p.
// The etc1_encode_block input stores pixel (x,y) at offset 3*(x + 4*y), where
// x = column = p/4 and y = row = p%4.
static inline int pixelOff(int p) {
    return 3 * ((p >> 2) + 4 * (p & 3));
}

// Column-major pixel positions for each sub-block, for flip=0 (left/right) and
// flip=1 (top/bottom) splits.
// Position p: column x = p/4, row y = p%4.
static const int kSub0Flip0[8] = {  0,  1,  2,  3,  4,  5,  6,  7 }; // x in {0,1}
static const int kSub1Flip0[8] = {  8,  9, 10, 11, 12, 13, 14, 15 }; // x in {2,3}
static const int kSub0Flip1[8] = {  0,  1,  4,  5,  8,  9, 12, 13 }; // y in {0,1}
static const int kSub1Flip1[8] = {  2,  3,  6,  7, 10, 11, 14, 15 }; // y in {2,3}

// Find the best modifier-table index for each pixel in a sub-block given the
// 8-bit base (R,G,B) color and a codeword.  Returns total squared error.
static int findBestIndices(const etc1_byte* pIn, const int* pos, int count,
                           int br, int bg, int bb, int table, int* out) {
    const int* m = kModifierTable[table];
    int total = 0;
    for (int i = 0; i < count; i++) {
        int off = pixelOff(pos[i]);
        int r = pIn[off], g = pIn[off + 1], b = pIn[off + 2];
        int best_err = 0x7fffffff, best_idx = 0;
        for (int idx = 0; idx < 4; idx++) {
            int dr = clamp_byte(br + m[idx]) - r;
            int dg = clamp_byte(bg + m[idx]) - g;
            int db = clamp_byte(bb + m[idx]) - b;
            int err = dr*dr + dg*dg + db*db;
            if (err < best_err) { best_err = err; best_idx = idx; }
        }
        if (out) out[i] = best_idx;
        total += best_err;
    }
    return total;
}

// Try all 8 codewords and return the one with minimum squared error.
static int findBestTable(const etc1_byte* pIn, const int* pos, int count,
                         int br, int bg, int bb, int* out_table, int* out_idx) {
    int best = 0x7fffffff;
    int tmp[8];
    *out_table = 0;
    for (int t = 0; t < 8; t++) {
        int err = findBestIndices(pIn, pos, count, br, bg, bb, t, tmp);
        if (err < best) {
            best = err;
            *out_table = t;
            if (out_idx)
                for (int i = 0; i < count; i++) out_idx[i] = tmp[i];
        }
    }
    return best;
}

// Compute the truncated average (R,G,B) for a set of pixels.
static void avgColor(const etc1_byte* pIn, const int* pos, int count,
                     int* r, int* g, int* b) {
    int sr = 0, sg = 0, sb = 0;
    for (int i = 0; i < count; i++) {
        int off = pixelOff(pos[i]);
        sr += pIn[off]; sg += pIn[off + 1]; sb += pIn[off + 2];
    }
    *r = sr / count; *g = sg / count; *b = sb / count;
}

etc1_uint32 etc1_get_encoded_data_size(etc1_uint32 width, etc1_uint32 height) {
    return ((width + 3) >> 2) * ((height + 3) >> 2) * ETC1_ENCODED_BLOCK_SIZE;
}

void etc1_encode_block(const etc1_byte* pIn, etc1_uint32 validPixelMask, etc1_byte* pOut) {
    (void)validPixelMask; // packer always passes 0xffff; all pixels treated as valid

    // Best candidate across all (flip, mode) combinations.
    struct {
        int r1, g1, b1;   // raw quantised values for sub-block 1
        int r2, g2, b2;   // raw quantised / delta values for sub-block 2
        int table1, table2;
        int indices[16];  // modifier index for each column-major output position
        int diff, flip;
        int error;
    } best;
    best.error = 0x7fffffff;

    for (int flip = 0; flip <= 1; flip++) {
        const int* sub1 = flip ? kSub0Flip1 : kSub0Flip0;
        const int* sub2 = flip ? kSub1Flip1 : kSub1Flip0;

        int avg_r1, avg_g1, avg_b1, avg_r2, avg_g2, avg_b2;
        avgColor(pIn, sub1, 8, &avg_r1, &avg_g1, &avg_b1);
        avgColor(pIn, sub2, 8, &avg_r2, &avg_g2, &avg_b2);

        // ---- Individual mode (4-bit per channel) ----
        int ir1 = (avg_r1 + 8) >> 4; if (ir1 > 15) ir1 = 15;
        int ig1 = (avg_g1 + 8) >> 4; if (ig1 > 15) ig1 = 15;
        int ib1 = (avg_b1 + 8) >> 4; if (ib1 > 15) ib1 = 15;
        int ir2 = (avg_r2 + 8) >> 4; if (ir2 > 15) ir2 = 15;
        int ig2 = (avg_g2 + 8) >> 4; if (ig2 > 15) ig2 = 15;
        int ib2 = (avg_b2 + 8) >> 4; if (ib2 > 15) ib2 = 15;

        int t1, t2, idx1[8], idx2[8];
        int e1 = findBestTable(pIn, sub1, 8, scale4to8(ir1), scale4to8(ig1), scale4to8(ib1), &t1, idx1);
        int e2 = findBestTable(pIn, sub2, 8, scale4to8(ir2), scale4to8(ig2), scale4to8(ib2), &t2, idx2);
        if (e1 + e2 < best.error) {
            best.error = e1 + e2;
            best.r1 = ir1; best.g1 = ig1; best.b1 = ib1;
            best.r2 = ir2; best.g2 = ig2; best.b2 = ib2;
            best.table1 = t1; best.table2 = t2;
            best.diff = 0; best.flip = flip;
            for (int i = 0; i < 8; i++) best.indices[sub1[i]] = idx1[i];
            for (int i = 0; i < 8; i++) best.indices[sub2[i]] = idx2[i];
        }

        // ---- Differential mode (5-bit base + 3-bit signed delta) ----
        int dr1 = (avg_r1 + 4) >> 3; if (dr1 > 31) dr1 = 31;
        int dg1 = (avg_g1 + 4) >> 3; if (dg1 > 31) dg1 = 31;
        int db1 = (avg_b1 + 4) >> 3; if (db1 > 31) db1 = 31;
        int dr2 = (avg_r2 + 4) >> 3; if (dr2 > 31) dr2 = 31;
        int dg2 = (avg_g2 + 4) >> 3; if (dg2 > 31) dg2 = 31;
        int db2 = (avg_b2 + 4) >> 3; if (db2 > 31) db2 = 31;

        int delta_r = dr2 - dr1, delta_g = dg2 - dg1, delta_b = db2 - db1;
        if (delta_r >= -4 && delta_r <= 3 &&
            delta_g >= -4 && delta_g <= 3 &&
            delta_b >= -4 && delta_b <= 3) {
            int dt1, dt2, didx1[8], didx2[8];
            int de1 = findBestTable(pIn, sub1, 8,
                                    scale5to8(dr1), scale5to8(dg1), scale5to8(db1), &dt1, didx1);
            int de2 = findBestTable(pIn, sub2, 8,
                                    scale5to8(dr1 + delta_r), scale5to8(dg1 + delta_g), scale5to8(db1 + delta_b),
                                    &dt2, didx2);
            if (de1 + de2 < best.error) {
                best.error = de1 + de2;
                best.r1 = dr1; best.g1 = dg1; best.b1 = db1;
                // store 3-bit two's-complement deltas
                best.r2 = delta_r & 7; best.g2 = delta_g & 7; best.b2 = delta_b & 7;
                best.table1 = dt1; best.table2 = dt2;
                best.diff = 1; best.flip = flip;
                for (int i = 0; i < 8; i++) best.indices[sub1[i]] = didx1[i];
                for (int i = 0; i < 8; i++) best.indices[sub2[i]] = didx2[i];
            }
        }
    }

    // ---- Write output bytes ----
    if (best.diff == 0) {
        // Individual mode: 4-bit colours packed into bytes 0-2
        pOut[0] = (etc1_byte)((best.r1 << 4) | best.r2);
        pOut[1] = (etc1_byte)((best.g1 << 4) | best.g2);
        pOut[2] = (etc1_byte)((best.b1 << 4) | best.b2);
    } else {
        // Differential mode: 5-bit base + 3-bit delta
        pOut[0] = (etc1_byte)((best.r1 << 3) | (best.r2 & 7));
        pOut[1] = (etc1_byte)((best.g1 << 3) | (best.g2 & 7));
        pOut[2] = (etc1_byte)((best.b1 << 3) | (best.b2 & 7));
    }
    // Byte 3: [table1(7:5) table2(4:2) diff(1) flip(0)]
    pOut[3] = (etc1_byte)((best.table1 << 5) | (best.table2 << 2) | (best.diff << 1) | best.flip);

    // Pixel index bits: pixel p at column-major position 0..15.
    // Packed big-endian: pixel 0 at MSB (bit 15) of each 16-bit halfword.
    // Bytes 4-5: MSBs; bytes 6-7: LSBs.
    uint16_t msb = 0, lsb = 0;
    for (int p = 0; p < 16; p++) {
        int idx = best.indices[p];
        msb = (uint16_t)((msb << 1) | ((idx >> 1) & 1));
        lsb = (uint16_t)((lsb << 1) | (idx & 1));
    }
    pOut[4] = (etc1_byte)(msb >> 8);
    pOut[5] = (etc1_byte)(msb & 0xFF);
    pOut[6] = (etc1_byte)(lsb >> 8);
    pOut[7] = (etc1_byte)(lsb & 0xFF);
}

int etc1_encode_image(const etc1_byte* pIn, etc1_uint32 width, etc1_uint32 height,
        etc1_uint32 pixelSize, etc1_uint32 stride, etc1_byte* pOut) {
    if (pixelSize != 2 && pixelSize != 3) return -1;

    etc1_uint32 encW = (width  + 3) & ~3u;
    etc1_uint32 encH = (height + 3) & ~3u;
    etc1_byte block[ETC1_DECODED_BLOCK_SIZE]; // 48 bytes, row-major RGB

    for (etc1_uint32 y = 0; y < encH; y += 4) {
        for (etc1_uint32 x = 0; x < encW; x += 4) {
            // Extract 4x4 RGB block into `block` buffer.
            // block layout: pixel (bx,by) at 3*(bx + 4*by) (column bx, row by).
            for (int bx = 0; bx < 4; bx++) {
                for (int by = 0; by < 4; by++) {
                    etc1_uint32 sx = x + (etc1_uint32)bx;
                    etc1_uint32 sy = y + (etc1_uint32)by;
                    int dst = 3 * (bx + 4 * by);
                    if (sx < width && sy < height) {
                        const etc1_byte* src = pIn + sy * stride + sx * pixelSize;
                        if (pixelSize == 3) {
                            block[dst]     = src[0];
                            block[dst + 1] = src[1];
                            block[dst + 2] = src[2];
                        } else {
                            // GL_UNSIGNED_SHORT_5_6_5 little-endian
                            uint16_t px = (uint16_t)(src[0] | (src[1] << 8));
                            block[dst]     = (etc1_byte)(((px >> 11) & 0x1F) * 255 / 31);
                            block[dst + 1] = (etc1_byte)(((px >>  5) & 0x3F) * 255 / 63);
                            block[dst + 2] = (etc1_byte)( (px        & 0x1F) * 255 / 31);
                        }
                    } else {
                        block[dst] = block[dst + 1] = block[dst + 2] = 0;
                    }
                }
            }
            etc1_encode_block(block, 0xffff, pOut);
            pOut += ETC1_ENCODED_BLOCK_SIZE;
        }
    }
    return 0;
}

void etc1_decode_block(const etc1_byte* pIn, etc1_byte* pOut) {
    int diff  = (pIn[3] >> 1) & 1;
    int flip  =  pIn[3]       & 1;
    int table1 = (pIn[3] >> 5) & 7;
    int table2 = (pIn[3] >> 2) & 7;

    int r1, g1, b1, r2, g2, b2;
    if (!diff) {
        r1 = scale4to8((pIn[0] >> 4) & 0xF);  r2 = scale4to8(pIn[0] & 0xF);
        g1 = scale4to8((pIn[1] >> 4) & 0xF);  g2 = scale4to8(pIn[1] & 0xF);
        b1 = scale4to8((pIn[2] >> 4) & 0xF);  b2 = scale4to8(pIn[2] & 0xF);
    } else {
        int R  = (pIn[0] >> 3) & 0x1F, dR = pIn[0] & 7; if (dR >= 4) dR -= 8;
        int G  = (pIn[1] >> 3) & 0x1F, dG = pIn[1] & 7; if (dG >= 4) dG -= 8;
        int B  = (pIn[2] >> 3) & 0x1F, dB = pIn[2] & 7; if (dB >= 4) dB -= 8;
        r1 = scale5to8(R);      g1 = scale5to8(G);      b1 = scale5to8(B);
        r2 = scale5to8(R + dR); g2 = scale5to8(G + dG); b2 = scale5to8(B + dB);
    }

    uint16_t msb = (uint16_t)((pIn[4] << 8) | pIn[5]);
    uint16_t lsb = (uint16_t)((pIn[6] << 8) | pIn[7]);

    for (int p = 0; p < 16; p++) {
        int shift = 15 - p;
        int idx = (((msb >> shift) & 1) << 1) | ((lsb >> shift) & 1);

        int bx = p >> 2;  // column
        int by = p & 3;   // row
        int inSub2 = flip ? (by >= 2) : (bx >= 2);

        int br = inSub2 ? r2 : r1;
        int bg = inSub2 ? g2 : g1;
        int bb = inSub2 ? b2 : b1;
        int tc = inSub2 ? table2 : table1;
        int mod = kModifierTable[tc][idx];

        // Output row-major: pixel (bx,by) at 3*(bx + 4*by)
        int dst = 3 * (bx + 4 * by);
        pOut[dst]     = (etc1_byte)clamp_byte(br + mod);
        pOut[dst + 1] = (etc1_byte)clamp_byte(bg + mod);
        pOut[dst + 2] = (etc1_byte)clamp_byte(bb + mod);
    }
}

int etc1_decode_image(const etc1_byte* pIn, etc1_byte* pOut,
        etc1_uint32 width, etc1_uint32 height,
        etc1_uint32 pixelSize, etc1_uint32 stride) {
    if (pixelSize != 2 && pixelSize != 3) return -1;
    etc1_byte block[ETC1_DECODED_BLOCK_SIZE];
    for (etc1_uint32 y = 0; y < height; y += 4) {
        for (etc1_uint32 x = 0; x < width; x += 4) {
            etc1_decode_block(pIn, block);
            pIn += ETC1_ENCODED_BLOCK_SIZE;
            for (int bx = 0; bx < 4 && x + (etc1_uint32)bx < width; bx++) {
                for (int by = 0; by < 4 && y + (etc1_uint32)by < height; by++) {
                    const etc1_byte* src = block + 3 * (bx + 4 * by);
                    etc1_byte* dst = pOut + (y + (etc1_uint32)by) * stride + (x + (etc1_uint32)bx) * pixelSize;
                    if (pixelSize == 3) {
                        dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
                    } else {
                        uint16_t r = (uint16_t)(src[0] >> 3);
                        uint16_t g = (uint16_t)(src[1] >> 2);
                        uint16_t b = (uint16_t)(src[2] >> 3);
                        uint16_t px = (uint16_t)((r << 11) | (g << 5) | b);
                        dst[0] = (etc1_byte)(px & 0xFF);
                        dst[1] = (etc1_byte)(px >> 8);
                    }
                }
            }
        }
    }
    return 0;
}

// PKM header: 16 bytes.  Magic "PKM 10", then encoded W/H, then original W/H (big-endian).
void etc1_pkm_format_header(etc1_byte* h, etc1_uint32 width, etc1_uint32 height) {
    h[0] = 'P'; h[1] = 'K'; h[2] = 'M'; h[3] = ' '; h[4] = '1'; h[5] = '0';
    h[6] = 0; h[7] = 0;
    etc1_uint32 ew = (width  + 3) & ~3u;
    etc1_uint32 eh = (height + 3) & ~3u;
    h[ 8] = (etc1_byte)(ew >> 8); h[ 9] = (etc1_byte)(ew & 0xFF);
    h[10] = (etc1_byte)(eh >> 8); h[11] = (etc1_byte)(eh & 0xFF);
    h[12] = (etc1_byte)(width  >> 8); h[13] = (etc1_byte)(width  & 0xFF);
    h[14] = (etc1_byte)(height >> 8); h[15] = (etc1_byte)(height & 0xFF);
}

etc1_bool etc1_pkm_is_valid(const etc1_byte* h) {
    return h[0]=='P' && h[1]=='K' && h[2]=='M' && h[3]==' ' && h[4]=='1' && h[5]=='0';
}

etc1_uint32 etc1_pkm_get_width(const etc1_byte* h) {
    return ((etc1_uint32)h[12] << 8) | h[13];
}

etc1_uint32 etc1_pkm_get_height(const etc1_byte* h) {
    return ((etc1_uint32)h[14] << 8) | h[15];
}
