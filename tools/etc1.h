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

#ifndef ETC1_H_INCLUDED
#define ETC1_H_INCLUDED

#define ETC1_ENCODED_BLOCK_SIZE 8
#define ETC1_DECODED_BLOCK_SIZE 48

#ifndef ETC1_RGB8_OES
#define ETC1_RGB8_OES 0x8D64
#endif

typedef unsigned char  etc1_byte;
typedef int            etc1_bool;
typedef unsigned int   etc1_uint32;

#ifdef __cplusplus
extern "C" {
#endif

// Encode a block of pixels.
// pIn:  ETC1_DECODED_BLOCK_SIZE bytes; pixel (x,y) R value is at pIn[3*(x+4*y)].
// validPixelMask: bit (1 << (x + y*4)) set means pixel is valid.
// pOut: ETC1_ENCODED_BLOCK_SIZE bytes of compressed output.
void etc1_encode_block(const etc1_byte* pIn, etc1_uint32 validPixelMask, etc1_byte* pOut);

// Decode a block of pixels.
void etc1_decode_block(const etc1_byte* pIn, etc1_byte* pOut);

// Return encoded image size (does not include PKM header).
etc1_uint32 etc1_get_encoded_data_size(etc1_uint32 width, etc1_uint32 height);

// Encode an entire image.
// pixelSize: 2 (GL_UNSIGNED_SHORT_5_6_5) or 3 (GL_BYTE RGB).
// Returns non-zero on error.
int etc1_encode_image(const etc1_byte* pIn, etc1_uint32 width, etc1_uint32 height,
        etc1_uint32 pixelSize, etc1_uint32 stride, etc1_byte* pOut);

// Decode an entire image.
int etc1_decode_image(const etc1_byte* pIn, etc1_byte* pOut,
        etc1_uint32 width, etc1_uint32 height,
        etc1_uint32 pixelSize, etc1_uint32 stride);

// PKM header helpers.
#define ETC_PKM_HEADER_SIZE 16
void       etc1_pkm_format_header(etc1_byte* pHeader, etc1_uint32 width, etc1_uint32 height);
etc1_bool  etc1_pkm_is_valid(const etc1_byte* pHeader);
etc1_uint32 etc1_pkm_get_width(const etc1_byte* pHeader);
etc1_uint32 etc1_pkm_get_height(const etc1_byte* pHeader);

#ifdef __cplusplus
}
#endif

#endif // ETC1_H_INCLUDED
