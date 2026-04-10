#include "../src/core/ResourceTypes.h"
#include "../src/core/hash.h"
#include <SDL3/SDL_stdinc.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <sys/stat.h>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <functional>
#include <png.h>
#include <squish.h>
#include <cassert>
#include <algorithm>
#include <json/json.h>
#include "../src/compress/Compress.h"
#ifdef ENABLE_ETC
#include <android/ETC1/etc1.h>
#endif

using namespace std;

// Extract relative path from full path for resource identification
// Everything in res.pak should be in the res/ folder, so we just need to
// extract the path starting from res/
string getRelativePath(const string& fullPath) {
    filesystem::path p(fullPath);
    string pathStr = p.string();

    // For .spv shader files in build directory, use res/shaders/ prefix
    if (p.extension() == ".spv") {
        return "res/shaders/" + p.filename().string();
    }

    // Look for /res/ directory marker and return everything from res/
    Uint64 pos = pathStr.find("/res/");
    if (pos != string::npos) {
        return pathStr.substr(pos + 1); // Return "res/..."
    }

    // Fallback to basename (shouldn't happen if everything is in res/)
    return p.filename().string();
}

struct FileInfo {
    string filename;
    Uint64 id;
    time_t mtime;
    vector<char> data;
    vector<char> compressedData;
    Uint32 compressionType;
    Uint32 decompressedSize;
    bool changed;
    Uint64 offset; // for unchanged files
};

// Structure for raw PNG image data before atlas packing
struct PNGImageData {
    string filename;
    Uint64 id;
    time_t mtime;
    vector<uint8_t> imageData;  // RGBA data
    Uint32 width;
    Uint32 height;
    bool hasAlpha;
};

// Rectangle for bin packing
struct PackRect {
    Uint32 width;
    Uint32 height;
    Uint32 x;
    Uint32 y;
    Uint64 imageIndex;  // Index into PNGImageData array
    bool packed;
};

// Simple maxrects bin packing algorithm
class MaxRectsBinPacker {
public:
    MaxRectsBinPacker(Uint32 binWidth, Uint32 binHeight)
        : binWidth_(binWidth), binHeight_(binHeight) {
        // Start with one free rect covering entire bin
        freeRects_.push_back({0, 0, binWidth, binHeight});
    }

    // Try to pack a rectangle, returns true if successful
    bool pack(PackRect& rect) {
        int bestIndex = -1;
        Uint32 bestShortSide = UINT32_MAX;

        // Find best free rect using Best Short Side Fit heuristic
        for (Uint64 i = 0; i < freeRects_.size(); i++) {
            FreeRect& freeRect = freeRects_[i];

            if (rect.width <= freeRect.width && rect.height <= freeRect.height) {
                Uint32 shortSide = min(freeRect.width - rect.width, freeRect.height - rect.height);
                if (shortSide < bestShortSide) {
                    bestShortSide = shortSide;
                    bestIndex = (int)i;
                    rect.x = freeRect.x;
                    rect.y = freeRect.y;
                }
            }
        }

        if (bestIndex < 0) {
            return false;  // Couldn't fit
        }

        rect.packed = true;
        splitFreeRect(bestIndex, rect);
        return true;
    }

private:
    struct FreeRect {
        Uint32 x, y, width, height;
    };

    vector<FreeRect> freeRects_;
    Uint32 binWidth_;
    Uint32 binHeight_;

    void splitFreeRect(int index, const PackRect& usedRect) {
        // Process ALL free rectangles and split any that overlap with the used rectangle
        Uint64 numRects = freeRects_.size();
        for (Uint64 i = 0; i < numRects; i++) {
            if (splitSingleFreeRectByUsedRect(i, usedRect)) {
                freeRects_.erase(freeRects_.begin() + i);
                i--;
                numRects--;
            }
        }

        pruneFreeRects();
    }

    bool splitSingleFreeRectByUsedRect(Uint64 index, const PackRect& usedRect) {
        FreeRect freeRect = freeRects_[index];

        // Check if used rectangle intersects with this free rectangle
        if (usedRect.x >= freeRect.x + freeRect.width || usedRect.x + usedRect.width <= freeRect.x ||
            usedRect.y >= freeRect.y + freeRect.height || usedRect.y + usedRect.height <= freeRect.y) {
            return false;
        }

        // Split the free rectangle into up to 4 new rectangles around the used one
        if (usedRect.x > freeRect.x) {
            FreeRect newRect;
            newRect.x = freeRect.x;
            newRect.y = freeRect.y;
            newRect.width = usedRect.x - freeRect.x;
            newRect.height = freeRect.height;
            freeRects_.push_back(newRect);
        }

        if (usedRect.x + usedRect.width < freeRect.x + freeRect.width) {
            FreeRect newRect;
            newRect.x = usedRect.x + usedRect.width;
            newRect.y = freeRect.y;
            newRect.width = freeRect.x + freeRect.width - newRect.x;
            newRect.height = freeRect.height;
            freeRects_.push_back(newRect);
        }

        if (usedRect.y > freeRect.y) {
            FreeRect newRect;
            newRect.x = freeRect.x;
            newRect.y = freeRect.y;
            newRect.width = freeRect.width;
            newRect.height = usedRect.y - freeRect.y;
            freeRects_.push_back(newRect);
        }

        if (usedRect.y + usedRect.height < freeRect.y + freeRect.height) {
            FreeRect newRect;
            newRect.x = freeRect.x;
            newRect.y = usedRect.y + usedRect.height;
            newRect.width = freeRect.width;
            newRect.height = freeRect.y + freeRect.height - newRect.y;
            freeRects_.push_back(newRect);
        }

        return true;
    }

    void pruneFreeRects() {
        // Remove any free rects fully contained within another
        for (Uint64 i = 0; i < freeRects_.size(); i++) {
            for (Uint64 j = i + 1; j < freeRects_.size(); ) {
                if (isContainedIn(freeRects_[j], freeRects_[i])) {
                    freeRects_.erase(freeRects_.begin() + j);
                } else if (isContainedIn(freeRects_[i], freeRects_[j])) {
                    freeRects_.erase(freeRects_.begin() + i);
                    i--;
                    break;
                } else {
                    j++;
                }
            }
        }
    }

    bool isContainedIn(const FreeRect& a, const FreeRect& b) {
        return a.x >= b.x && a.y >= b.y &&
               a.x + a.width <= b.x + b.width &&
               a.y + a.height <= b.y + b.height;
    }
};

// Atlas structure to hold packed images
struct TextureAtlas {
    Uint64 atlasId;
    Uint32 width;
    Uint32 height;
    bool hasAlpha;
    vector<uint8_t> imageData;  // RGBA atlas image
    vector<AtlasEntry> entries;
    vector<Uint64> packedImageIndices;  // Indices of images packed into this atlas
};

bool loadFile(const string& filename, vector<char>& data, time_t& mtime) {
    struct stat st;
    if (stat(filename.c_str(), &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;  // Skip directories and special files
    mtime = st.st_mtime;
    ifstream file(filename, ios::binary | ios::ate);
    if (!file) return false;
    Uint64 size = file.tellg();
    file.seekg(0);
    data.resize(size);
    file.read(data.data(), size);
    return true;
}

void compressData(const vector<char>& input, vector<char>& output, Uint32& compressionType) {
    size_t maxCompressedSize = Compress::maxSize(input.size());
    output.resize(maxCompressedSize);
    size_t compressedSize = Compress::compress(input.data(), input.size(), output.data(), maxCompressedSize);
    if (compressedSize > 0 && compressedSize < input.size()) {
        output.resize((size_t)compressedSize);
        compressionType = COMPRESSION_FLAGS_CMPR;
    } else {
        // Compression didn't help, store uncompressed
        output = input;
        compressionType = COMPRESSION_FLAGS_UNCOMPRESSED;
    }
}

Uint32 getFileType(const string& filename, bool isAtlas = false) {
    if (isAtlas) return RESOURCE_TYPE_IMAGE_ATLAS;
    string ext = filesystem::path(filename).extension().string();
    if (ext == ".lua") return RESOURCE_TYPE_LUA;
    if (ext == ".spv") return RESOURCE_TYPE_SHADER;
    if (ext == ".png") return RESOURCE_TYPE_IMAGE;
    if (ext == ".opus") return RESOURCE_TYPE_SOUND;
    if (ext == ".loop") return RESOURCE_TYPE_MUSIC_TRACK;
    // Add more extensions as needed
    return RESOURCE_TYPE_UNKNOWN;
}

#ifdef ENABLE_ETC
// Round up to next multiple of 4 for DXT/ETC block alignment
static Uint32 alignTo4(Uint32 val) {
    return (val + 3) & ~3;
}

// EAC alpha modifier tables from OpenGL ES 3.0 specification Table C.4.3
// Used for ETC2 RGBA alpha channel compression
static const int kEACModifiers[16][8] = {
    { -3,  -6,  -9, -15,  2,  5,  8, 14 },
    { -3,  -7, -10, -13,  2,  6,  9, 12 },
    { -2,  -5,  -8, -13,  1,  4,  7, 12 },
    { -2,  -4,  -6, -13,  1,  3,  5, 12 },
    { -3,  -6,  -8, -15,  2,  5,  7, 14 },
    { -4,  -7,  -8, -15,  3,  6,  7, 14 },
    { -3,  -5,  -8, -13,  2,  4,  7, 12 },
    { -4,  -6,  -8, -13,  3,  5,  7, 12 },
    { -4,  -8,  -8, -15,  3,  7,  7, 14 },
    { -2,  -5,  -8, -13,  1,  4,  7, 12 },
    { -2,  -4,  -8, -13,  1,  3,  7, 12 },
    { -3,  -4,  -8, -13,  2,  3,  7, 12 },
    { -4,  -6,  -8, -15,  3,  5,  7, 14 },
    { -4,  -8,  -8, -15,  3,  7,  7, 14 },
    { -4,  -8,  -8, -15,  3,  7,  7, 14 },
    { -4,  -8,  -8, -15,  3,  7,  7, 14 }
};

// Encode one 4x4 block of alpha values to EAC (8 bytes output).
// alpha_block[y*4+x] is the alpha value at column x, row y (row-major input).
// Pixel ordering in EAC output is column-major: pixel i is at (x=i/4, y=i%4).
// Output is 8 bytes stored big-endian per the OpenGL ES 3.0 spec.
static void encodeEACAlphaBlock(const uint8_t* alpha_block, uint8_t* out) {
    // EAC uses column-major ordering: pixel i maps to row-major index (i%4)*4+(i/4)
    auto pixelAlpha = [&](int i) -> int {
        return alpha_block[(i % 4) * 4 + (i / 4)];
    };

    int best_error = 0x7fffffff;
    int best_base = 0;
    int best_mult = 0;
    int best_table = 0;
    uint64_t best_idx_bits = 0;

    // Compute range and mean of the 16 alpha values
    int min_a = 255, max_a = 0, sum = 0;
    for (int i = 0; i < 16; i++) {
        int a = pixelAlpha(i);
        if (a < min_a) min_a = a;
        if (a > max_a) max_a = a;
        sum += a;
    }
    int range = max_a - min_a;
    int mean = sum / 16;

    for (int t = 0; t < 16; t++) {
        // Find the range of this table's modifiers
        int mod_min = kEACModifiers[t][0], mod_max = kEACModifiers[t][7];
        for (int j = 0; j < 8; j++) {
            if (kEACModifiers[t][j] < mod_min) mod_min = kEACModifiers[t][j];
            if (kEACModifiers[t][j] > mod_max) mod_max = kEACModifiers[t][j];
        }
        int mod_range = mod_max - mod_min;

        // Determine optimal multiplier candidates based on alpha range
        int k_opt = (mod_range > 0) ? ((range + mod_range / 2) / mod_range) : 0;
        if (k_opt < 0) k_opt = 0;
        if (k_opt > 15) k_opt = 15;

        for (int dk = -1; dk <= 1; dk++) {
            int k = k_opt + dk;
            if (k < 0) k = 0;
            if (k > 15) k = 15;

            // Initial base estimate: shift min_a to align with most negative modifier
            int base;
            if (k == 0) {
                base = mean;
            } else {
                base = min_a - k * mod_min;
                if (base < 0) base = 0;
                if (base > 255) base = 255;
            }

            // Iterative refinement: assign indices then recompute base
            for (int iter = 0; iter < 4; iter++) {
                int base_sum = 0;
                for (int i = 0; i < 16; i++) {
                    int a = pixelAlpha(i);
                    int best_j = 0;
                    int enc = base + k * kEACModifiers[t][0];
                    if (enc < 0) enc = 0;
                    if (enc > 255) enc = 255;
                    int best_d = abs(enc - a);
                    for (int j = 1; j < 8; j++) {
                        enc = base + k * kEACModifiers[t][j];
                        if (enc < 0) enc = 0;
                        if (enc > 255) enc = 255;
                        int d = abs(enc - a);
                        if (d < best_d) {
                            best_d = d;
                            best_j = j;
                        }
                    }
                    base_sum += a - k * kEACModifiers[t][best_j];
                }
                base = base_sum / 16;
                if (base < 0) base = 0;
                if (base > 255) base = 255;
            }

            // Compute total error and build index bits for this (t, k, base)
            int error = 0;
            uint64_t idx_bits = 0;
            for (int i = 0; i < 16; i++) {
                int a = pixelAlpha(i);
                int best_j = 0;
                int enc0 = base + k * kEACModifiers[t][0];
                if (enc0 < 0) enc0 = 0;
                if (enc0 > 255) enc0 = 255;
                int best_d = (enc0 - a) * (enc0 - a);
                for (int j = 1; j < 8; j++) {
                    int enc = base + k * kEACModifiers[t][j];
                    if (enc < 0) enc = 0;
                    if (enc > 255) enc = 255;
                    int d = (enc - a) * (enc - a);
                    if (d < best_d) {
                        best_d = d;
                        best_j = j;
                    }
                }
                error += best_d;
                idx_bits = (idx_bits << 3) | (uint64_t)(best_j & 0x7);
            }

            if (error < best_error) {
                best_error = error;
                best_base = base;
                best_mult = k;
                best_table = t;
                best_idx_bits = idx_bits;
            }
        }
    }

    // Pack into 8 bytes (big-endian, per the OpenGL ES 3.0 EAC spec):
    //   byte 0: base_codeword
    //   byte 1: (multiplier << 4) | table_index
    //   bytes 2-7: 48 bits of per-pixel indices (3 bits each, 16 pixels)
    out[0] = (uint8_t)best_base;
    out[1] = (uint8_t)((best_mult << 4) | (best_table & 0x0f));
    for (int b = 0; b < 6; b++) {
        out[2 + b] = (uint8_t)((best_idx_bits >> (40 - 8 * b)) & 0xff);
    }
}

// Compress image data using ETC1 (RGB) or ETC2 RGBA compression.
// imageData must be RGBA (4 bytes per pixel).
void compressImageETC(const vector<uint8_t>& imageData, vector<char>& compressed,
                      Uint32 width, Uint32 height, bool hasAlpha, Uint16& format) {
    assert(width > 0 && height > 0);
    assert(imageData.size() == (size_t)width * height * 4);
    // Dimensions must be multiples of 4 for ETC block alignment
    assert((width % 4) == 0 && (height % 4) == 0);

    Uint32 blocksX = width / 4;
    Uint32 blocksY = height / 4;

    if (!hasAlpha) {
        // ETC1: RGB only.  Convert RGBA -> RGB then use etc1_encode_image.
        format = IMAGE_FORMAT_ETC1;
        size_t rgbSize = (size_t)width * height * 3;
        vector<uint8_t> rgb(rgbSize);
        for (Uint32 i = 0; i < width * height; i++) {
            rgb[i * 3 + 0] = imageData[i * 4 + 0];
            rgb[i * 3 + 1] = imageData[i * 4 + 1];
            rgb[i * 3 + 2] = imageData[i * 4 + 2];
        }
        size_t outSize = (size_t)etc1_get_encoded_data_size(width, height);
        if (outSize == 0) {
            cerr << "ETC1: etc1_get_encoded_data_size returned 0 for " << width << "x" << height << endl;
        }
        assert(outSize > 0);
        compressed.resize(outSize);
        int result = etc1_encode_image(rgb.data(), width, height, 3, width * 3,
                                       (etc1_byte*)compressed.data());
        if (result != 0) {
            cerr << "ETC1 encoding failed with result " << result
                 << " for " << width << "x" << height << " image" << endl;
        }
        assert(result == 0 && "ETC1 encoding failed");
    } else {
        // ETC2 RGBA: interleave [8-byte EAC alpha][8-byte ETC1 RGB] per block.
        format = IMAGE_FORMAT_ETC2;
        size_t outSize = (size_t)blocksX * blocksY * 16;
        if (outSize == 0) {
            cerr << "ETC2: computed output size is 0 for " << width << "x" << height
                 << " (" << blocksX << "x" << blocksY << " blocks)" << endl;
        }
        assert(outSize > 0);
        compressed.resize(outSize);

        for (Uint32 by = 0; by < blocksY; by++) {
            for (Uint32 bx = 0; bx < blocksX; bx++) {
                // Extract 4x4 alpha and RGB sub-blocks from the RGBA image.
                uint8_t alpha_block[16]; // row-major: [y*4+x]
                uint8_t rgb_block[48];   // etc1 row-major: [3*(x+4*y)]
                for (Uint32 py = 0; py < 4; py++) {
                    for (Uint32 px = 0; px < 4; px++) {
                        size_t src = ((by * 4 + py) * width + (bx * 4 + px)) * 4;
                        alpha_block[py * 4 + px] = imageData[src + 3];
                        size_t dst_rgb = (px + 4 * py) * 3;
                        rgb_block[dst_rgb + 0] = imageData[src + 0];
                        rgb_block[dst_rgb + 1] = imageData[src + 1];
                        rgb_block[dst_rgb + 2] = imageData[src + 2];
                    }
                }

                size_t block_offset = ((size_t)by * blocksX + bx) * 16;
                uint8_t* dst = (uint8_t*)compressed.data() + block_offset;

                // First 8 bytes: EAC-compressed alpha
                encodeEACAlphaBlock(alpha_block, dst);
                // Next 8 bytes: ETC1-compatible RGB
                etc1_encode_block(rgb_block, 0xffff, dst + 8);
            }
        }
    }
}
#endif // ENABLE_ETC

// Load PNG image and convert to RGBA
bool loadPNG(const string& filename, vector<uint8_t>& imageData, Uint32& width, Uint32& height, bool& hasAlpha) {
    FILE* fp = fopen(filename.c_str(), "rb");
    if (!fp) return false;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        fclose(fp);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    width = png_get_image_width(png, info);
    height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    // Error out if image is paletted or grayscale
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        cerr << "Error: Paletted PNG images are not supported: " << filename << endl;
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return false;
    }

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        cerr << "Error: Grayscale PNG images are not supported: " << filename << endl;
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return false;
    }

    // Only support RGB and RGBA
    if (color_type != PNG_COLOR_TYPE_RGB && color_type != PNG_COLOR_TYPE_RGBA) {
        cerr << "Error: Unsupported PNG color type: " << filename << endl;
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return false;
    }

    // Strip 16-bit to 8-bit
    if (bit_depth == 16)
        png_set_strip_16(png);

    png_read_update_info(png, info);

    // Determine if the image has alpha
    hasAlpha = (color_type == PNG_COLOR_TYPE_RGBA);

    // Allocate memory for image data
    int bytesPerPixel = hasAlpha ? 4 : 3;
    imageData.resize(width * height * bytesPerPixel);

    vector<png_bytep> row_pointers(height);
    for (Uint32 y = 0; y < height; y++) {
        row_pointers[y] = imageData.data() + y * width * bytesPerPixel;
    }

    png_read_image(png, row_pointers.data());
    png_read_end(png, nullptr);

    png_destroy_read_struct(&png, &info, nullptr);
    fclose(fp);

    return true;
}

// Save RGBA image data as PNG
bool savePNG(const string& filename, const vector<uint8_t>& imageData, Uint32 width, Uint32 height) {
    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) return false;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        fclose(fp);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);

    // Set image attributes
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    // Write image data
    vector<png_bytep> row_pointers(height);
    for (Uint32 y = 0; y < height; y++) {
        row_pointers[y] = (png_bytep)(imageData.data() + y * width * 4);
    }

    png_write_image(png, row_pointers.data());
    png_write_end(png, nullptr);

    png_destroy_write_struct(&png, &info);
    fclose(fp);

    return true;
}

// Compress image data using DXT/BC or ETC1/ETC2 compression.
// imageData must be RGBA (4 bytes per pixel).
void compressImage(const vector<uint8_t>& imageData, vector<char>& compressed, Uint32 width, Uint32 height, bool hasAlpha, Uint16& format, bool useETC) {
    assert(width > 0 && height > 0);
    // imageData is always RGBA (4 bytes per pixel) at this point
    assert(imageData.size() == width * height * 4);

    if (useETC) {
#ifdef ENABLE_ETC
        // ETC requires dimensions to be multiples of 4; pad if necessary
        Uint32 paddedW = alignTo4(width);
        Uint32 paddedH = alignTo4(height);
        if (paddedW != width || paddedH != height) {
            vector<uint8_t> padded(paddedW * paddedH * 4, 0);
            for (Uint32 y = 0; y < height; y++) {
                memcpy(padded.data() + y * paddedW * 4,
                       imageData.data() + y * width * 4,
                       width * 4);
            }
            compressImageETC(padded, compressed, paddedW, paddedH, hasAlpha, format);
        } else {
            compressImageETC(imageData, compressed, width, height, hasAlpha, format);
        }
        return;
#else
        assert(false && "ETC compression not supported in this build");
        return;
#endif
    }

    // Choose compression format based on alpha channel
    int flags;
    if (hasAlpha) {
        flags = squish::kDxt5;  // BC3/DXT5 for RGBA
        format = IMAGE_FORMAT_BC3_DXT5;
    } else {
        flags = squish::kDxt1;  // BC1/DXT1 for RGB
        format = IMAGE_FORMAT_BC1_DXT1;
    }

    // Calculate storage requirements
    int storageSize = squish::GetStorageRequirements(width, height, flags);
    assert(storageSize > 0);
    compressed.resize(storageSize);

    // Compress RGBA directly
    squish::CompressImage(imageData.data(), width, height, compressed.data(), flags);
}

// Compress image data from raw format (RGB or RGBA)
void compressImageRaw(const vector<uint8_t>& imageData, vector<char>& compressed, Uint32 width, Uint32 height, bool hasAlpha, Uint16& format, bool useETC) {
    assert(width > 0 && height > 0);
    int bytesPerPixel = hasAlpha ? 4 : 3;
    assert(imageData.size() == width * height * bytesPerPixel);

    // Convert to RGBA if needed
    vector<uint8_t> rgbaData;
    if (!hasAlpha) {
        rgbaData.resize(width * height * 4);
        for (Uint32 i = 0; i < width * height; i++) {
            rgbaData[i * 4 + 0] = imageData[i * 3 + 0];  // R
            rgbaData[i * 4 + 1] = imageData[i * 3 + 1];  // G
            rgbaData[i * 4 + 2] = imageData[i * 3 + 2];  // B
            rgbaData[i * 4 + 3] = 255;                   // A (fully opaque)
        }
        compressImage(rgbaData, compressed, width, height, false, format, useETC);
    } else {
        compressImage(imageData, compressed, width, height, true, format, useETC);
    }
}

// Pack multiple PNG images into texture atlases
// Returns the number of atlases created
Uint64 packImagesIntoAtlases(vector<PNGImageData>& images, vector<TextureAtlas>& atlases, Uint32 maxAtlasSize) {
    if (images.empty()) return 0;

    // Sort all images by area (descending) for better bin packing
    // Larger images should be placed first
    vector<Uint64> sortedIndices(images.size());
    for (Uint64 i = 0; i < images.size(); i++) {
        sortedIndices[i] = i;
    }
    sort(sortedIndices.begin(), sortedIndices.end(), [&images](Uint64 a, Uint64 b) {
        Uint32 areaA = images[a].width * images[a].height;
        Uint32 areaB = images[b].width * images[b].height;
        return areaA > areaB;
    });

    // Prepare rectangles for all images
    // Add 2 pixels (1 on each side) for edge padding to prevent texture bleeding
    const Uint32 EDGE_PADDING = 1;
    vector<PackRect> rects;
    rects.reserve(images.size());
    for (Uint64 idx : sortedIndices) {
        PackRect rect;
        // Add 2 pixels for edge padding (1 on each side), then align to 4 pixels for DXT block boundaries
        rect.width = alignTo4(images[idx].width + EDGE_PADDING * 2);
        rect.height = alignTo4(images[idx].height + EDGE_PADDING * 2);
        rect.imageIndex = idx;
        rect.packed = false;
        rects.push_back(rect);
    }

    // Calculate total area needed (with some padding for inefficiency)
    Uint64 totalArea = 0;
    Uint32 maxImageW = 0, maxImageH = 0;
    for (const auto& rect : rects) {
        totalArea += (Uint64)rect.width * rect.height;
        maxImageW = max(maxImageW, rect.width);
        maxImageH = max(maxImageH, rect.height);
    }

    // Start with atlas size that can fit all images (estimate with 30% overhead)
    Uint32 estimatedSize = (Uint32)sqrt((double)totalArea * 1.3);
    Uint32 atlasWidth = 256;
    Uint32 atlasHeight = 256;
    while (atlasWidth < estimatedSize && atlasWidth < maxAtlasSize) atlasWidth *= 2;
    while (atlasHeight < estimatedSize && atlasHeight < maxAtlasSize) atlasHeight *= 2;
    // Ensure atlas is at least as large as the biggest image
    while (atlasWidth < maxImageW && atlasWidth < maxAtlasSize) atlasWidth *= 2;
    while (atlasHeight < maxImageH && atlasHeight < maxAtlasSize) atlasHeight *= 2;

    // Try to pack all images into atlases
    while (true) {
        // Find unpacked rects
        vector<Uint64> unpacked;
        for (Uint64 i = 0; i < rects.size(); i++) {
            if (!rects[i].packed) {
                unpacked.push_back(i);
            }
        }
        if (unpacked.empty()) break;

        // Try progressively larger atlas sizes until we can pack at least one image
        Uint32 tryWidth = atlasWidth;
        Uint32 tryHeight = atlasHeight;
        bool success = false;
        vector<Uint64> packedInThisAtlas;

        while (!success && tryWidth <= maxAtlasSize && tryHeight <= maxAtlasSize) {
            MaxRectsBinPacker packer(tryWidth, tryHeight);
            packedInThisAtlas.clear();

            // Try to pack all unpacked images into this atlas
            for (Uint64 i : unpacked) {
                PackRect testRect = rects[i];
                if (packer.pack(testRect)) {
                    rects[i] = testRect;  // Update with packed position
                    packedInThisAtlas.push_back(i);
                }
            }

            if (!packedInThisAtlas.empty()) {
                success = true;
            } else {
                // Grow atlas - prefer square-ish shapes
                if (tryWidth <= tryHeight && tryWidth * 2 <= maxAtlasSize) {
                    tryWidth *= 2;
                } else if (tryHeight * 2 <= maxAtlasSize) {
                    tryHeight *= 2;
                } else {
                    break;  // Can't grow further
                }
            }
        }

        if (!success) {
            // Images too large for atlas - mark remaining as standalone
            for (Uint64 i : unpacked) {
                rects[i].packed = true;  // Mark to avoid infinite loop
            }
            continue;
        }

        // Determine if this atlas needs alpha (BC3) or can use BC1
        bool atlasHasAlpha = false;
        for (Uint64 i : packedInThisAtlas) {
            if (images[rects[i].imageIndex].hasAlpha) {
                atlasHasAlpha = true;
                break;
            }
        }

        // Generate deterministic atlas ID based on content
        Uint64 atlasContentHash = 0;
        for (Uint64 i : packedInThisAtlas) {
            atlasContentHash ^= images[rects[i].imageIndex].id;
            atlasContentHash = (atlasContentHash << 7) | (atlasContentHash >> 57);
        }
        atlasContentHash ^= ((Uint64)tryWidth << 32) | tryHeight;
        atlasContentHash ^= atlasHasAlpha ? 0xFFFFFFFF : 0;

        // Create the atlas
        TextureAtlas atlas;
        atlas.atlasId = atlasContentHash;
        atlas.width = tryWidth;
        atlas.height = tryHeight;
        atlas.hasAlpha = atlasHasAlpha;
        atlas.imageData.resize(tryWidth * tryHeight * 4);
        // Initialize to hot pink (255, 0, 255, 255)
        for (Uint64 i = 0; i < atlas.imageData.size(); i += 4) {
            atlas.imageData[i] = 255;     // R
            atlas.imageData[i + 1] = 0;   // G
            atlas.imageData[i + 2] = 255; // B
            atlas.imageData[i + 3] = 255; // A
        }

        // Copy packed images into atlas with edge padding
        const Uint32 EDGE_PADDING = 1;
        for (Uint64 i : packedInThisAtlas) {
            PackRect& rect = rects[i];
            PNGImageData& img = images[rect.imageIndex];

            // Convert source image to RGBA if needed
            vector<uint8_t> rgbaSource;
            const uint8_t* srcData;
            if (img.hasAlpha) {
                srcData = img.imageData.data();
            } else {
                rgbaSource.resize(img.width * img.height * 4);
                for (Uint32 p = 0; p < img.width * img.height; p++) {
                    rgbaSource[p * 4 + 0] = img.imageData[p * 3 + 0];
                    rgbaSource[p * 4 + 1] = img.imageData[p * 3 + 1];
                    rgbaSource[p * 4 + 2] = img.imageData[p * 3 + 2];
                    rgbaSource[p * 4 + 3] = 255;
                }
                srcData = rgbaSource.data();
            }

            // Helper lambda to copy a pixel from source to atlas
            auto copyPixel = [&](Uint32 srcX, Uint32 srcY, Uint32 dstX, Uint32 dstY) {
                // Clamp source coordinates to valid range
                srcX = min(srcX, img.width - 1);
                srcY = min(srcY, img.height - 1);
                Uint32 srcIdx = (srcY * img.width + srcX) * 4;
                Uint32 dstIdx = (dstY * tryWidth + dstX) * 4;
                atlas.imageData[dstIdx + 0] = srcData[srcIdx + 0];
                atlas.imageData[dstIdx + 1] = srcData[srcIdx + 1];
                atlas.imageData[dstIdx + 2] = srcData[srcIdx + 2];
                atlas.imageData[dstIdx + 3] = srcData[srcIdx + 3];
            };

            // The actual image content starts at (rect.x + EDGE_PADDING, rect.y + EDGE_PADDING)
            Uint32 contentX = rect.x + EDGE_PADDING;
            Uint32 contentY = rect.y + EDGE_PADDING;

            // Copy main image content
            for (Uint32 y = 0; y < img.height; y++) {
                for (Uint32 x = 0; x < img.width; x++) {
                    copyPixel(x, y, contentX + x, contentY + y);
                }
            }

            // Duplicate edge pixels for padding (prevents texture bleeding)
            // Top edge padding (duplicate first row)
            for (Uint32 x = 0; x < img.width; x++) {
                copyPixel(x, 0, contentX + x, contentY - 1);
            }
            // Bottom edge padding (duplicate last row)
            for (Uint32 x = 0; x < img.width; x++) {
                copyPixel(x, img.height - 1, contentX + x, contentY + img.height);
            }
            // Left edge padding (duplicate first column)
            for (Uint32 y = 0; y < img.height; y++) {
                copyPixel(0, y, contentX - 1, contentY + y);
            }
            // Right edge padding (duplicate last column)
            for (Uint32 y = 0; y < img.height; y++) {
                copyPixel(img.width - 1, y, contentX + img.width, contentY + y);
            }
            // Corner padding (duplicate corner pixels)
            copyPixel(0, 0, contentX - 1, contentY - 1);  // Top-left
            copyPixel(img.width - 1, 0, contentX + img.width, contentY - 1);  // Top-right
            copyPixel(0, img.height - 1, contentX - 1, contentY + img.height);  // Bottom-left
            copyPixel(img.width - 1, img.height - 1, contentX + img.width, contentY + img.height);  // Bottom-right

            // Create atlas entry - point to actual content (offset by EDGE_PADDING)
            AtlasEntry entry;
            entry.originalId = img.id;
            entry.x = (Uint16)contentX;
            entry.y = (Uint16)contentY;
            entry.width = (Uint16)img.width;
            entry.height = (Uint16)img.height;
            atlas.entries.push_back(entry);
            atlas.packedImageIndices.push_back(rect.imageIndex);

            // Mark as packed
            rect.packed = true;
        }

        atlases.push_back(std::move(atlas));
    }

    return atlases.size();
}

// Process atlas: compress and create output data with AtlasHeader
bool processAtlas(TextureAtlas& atlas, vector<char>& output, bool useETC) {
    assert(atlas.width > 0 && atlas.height > 0);
    assert(atlas.imageData.size() == atlas.width * atlas.height * 4);

    // Compress the atlas image data
    vector<char> compressedImage;
    Uint16 format;
    compressImage(atlas.imageData, compressedImage, atlas.width, atlas.height, atlas.hasAlpha, format, useETC);

    assert(compressedImage.size() > 0);

    // Create AtlasHeader
    AtlasHeader header;
    header.format = format;
    header.width = (Uint16)atlas.width;
    header.height = (Uint16)atlas.height;
    header.numEntries = (Uint16)atlas.entries.size();

    // Calculate total size: header + entries + compressed data
    Uint64 entriesSize = sizeof(AtlasEntry) * atlas.entries.size();
    output.resize(sizeof(AtlasHeader) + entriesSize + compressedImage.size());

    // Write header
    memcpy(output.data(), &header, sizeof(AtlasHeader));

    // Write entries
    memcpy(output.data() + sizeof(AtlasHeader), atlas.entries.data(), entriesSize);

    // Write compressed image data
    memcpy(output.data() + sizeof(AtlasHeader) + entriesSize, compressedImage.data(), compressedImage.size());

    return true;
}

// Process PNG file: load, compress, and prepend ImageHeader
bool processPNGFile(const string& filename, vector<char>& output, bool useETC) {
    vector<uint8_t> imageData;
    Uint32 width, height;
    bool hasAlpha;

    if (!loadPNG(filename, imageData, width, height, hasAlpha)) {
        return false;
    }

    assert(width > 0 && height > 0);
    int bytesPerPixel = hasAlpha ? 4 : 3;
    assert(imageData.size() == width * height * bytesPerPixel);

    // Compress the image data
    vector<char> compressedImage;
    Uint16 format;
    compressImageRaw(imageData, compressedImage, width, height, hasAlpha, format, useETC);

    assert(compressedImage.size() > 0);

    // Create ImageHeader
    ImageHeader header;
    header.format = format;
    header.width = width;
    header.height = height;
    header.pad = 0;

    // Combine header and compressed data
    output.resize(sizeof(ImageHeader) + compressedImage.size());
    memcpy(output.data(), &header, sizeof(ImageHeader));
    memcpy(output.data() + sizeof(ImageHeader), compressedImage.data(), compressedImage.size());

    return true;
}

// Generate trig lookup table with entries for every half-degree
bool generateTrigTable(vector<char>& output) {
    const Uint32 numEntries = 720;  // 360 degrees / 0.5 degrees per entry
    const float angleStep = 3.14159265359f / 360.0f;  // PI/360 radians (0.5 degrees)

    cout << "Generating trig lookup table with " << numEntries << " entries" << endl;

    // Create header
    TrigTableHeader header;
    header.numEntries = numEntries;
    header.angleStep = angleStep;

    // Calculate total size: header + sin table + cos table
    Uint64 tableSize = numEntries * sizeof(float);
    Uint64 totalSize = sizeof(TrigTableHeader) + tableSize + tableSize;
    output.resize(totalSize);

    // Write header
    memcpy(output.data(), &header, sizeof(TrigTableHeader));

    // Generate sin and cos tables
    float* sinTable = (float*)(output.data() + sizeof(TrigTableHeader));
    float* cosTable = sinTable + numEntries;

    for (Uint32 i = 0; i < numEntries; ++i) {
        float angle = i * angleStep;
        sinTable[i] = sinf(angle);
        cosTable[i] = cosf(angle);
    }

    cout << "Trig lookup table generated: " << output.size() << " bytes" << endl;
    return true;
}

// Process a .loop JSON file into a binary MusicTrackHeader resource.
// layerDir is the directory portion of the .loop file's path used to resolve
// relative OPUS layer filenames.  loopRelDir is the corresponding "res/..."
// prefix used when computing resource IDs (e.g., "res/music/").
bool processLoopFile(const string& filename, vector<char>& output) {
    ifstream f(filename);
    if (!f) {
        cerr << "Failed to open loop file: " << filename << endl;
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    string parseErrors;
    if (!Json::parseFromStream(builder, f, &root, &parseErrors)) {
        cerr << "Failed to parse loop file " << filename << ":\n" << parseErrors << endl;
        return false;
    }

    Uint32 loopStart = root.get("loop_start", 0u).asUInt();
    Uint32 loopEnd   = root.get("loop_end",   0u).asUInt();

    const Json::Value& intensities = root["intensities"];
    if (!intensities.isArray()) {
        cerr << "Loop file missing 'intensities' array: " << filename << endl;
        return false;
    }

    Uint32 numIntensities = (Uint32)intensities.size();
    if (numIntensities == 0) {
        cerr << "Loop file has no intensities: " << filename << endl;
        return false;
    }

    // Collect per-intensity info and total layer count.
    vector<vector<Uint64>> intensityLayers(numIntensities);
    vector<Uint64>         intensityNames(numIntensities);
    Uint32 totalLayers = 0;

    for (Uint32 i = 0; i < numIntensities; i++) {
        const Json::Value& entry = intensities[i];
        string name = entry.get("name", "").asString();
        if (name.empty()) {
            cerr << "Intensity " << i << " has no name in " << filename << endl;
            return false;
        }
        intensityNames[i] = hashCString(name.c_str());

        const Json::Value& layers = entry["layers"];
        if (!layers.isArray() || layers.empty()) {
            cerr << "Intensity '" << name << "' has no layers in " << filename << endl;
            return false;
        }

        for (const Json::Value& layerVal : layers) {
            string layerFile = layerVal.asString();
            Uint64 id = hashCString(layerFile.c_str());
            intensityLayers[i].push_back(id);
        }
        totalLayers += (Uint32)intensityLayers[i].size();
    }

    // Compute total output size.
    Uint64 headerSize      = sizeof(MusicTrackHeader);
    Uint64 intensitySize   = sizeof(MusicIntensityInfo) * numIntensities;
    Uint64 layerIdsSize    = sizeof(Uint64) * totalLayers;
    Uint64 totalSize       = headerSize + intensitySize + layerIdsSize;
    output.resize(totalSize);
    char* ptr = output.data();

    // Write MusicTrackHeader.
    MusicTrackHeader hdr;
    hdr.loopStartSample = loopStart;
    hdr.loopEndSample   = loopEnd;
    hdr.numIntensities  = numIntensities;
    hdr.totalLayers     = totalLayers;
    memcpy(ptr, &hdr, sizeof(hdr));
    ptr += sizeof(hdr);

    // Write MusicIntensityInfo[].
    Uint32 layerStartIndex = 0;
    for (Uint32 i = 0; i < numIntensities; i++) {
        MusicIntensityInfo info;
        info.nameHash        = intensityNames[i];
        info.layerStartIndex = layerStartIndex;
        info.numLayers       = (Uint32)intensityLayers[i].size();
        info.pad             = 0;
        memcpy(ptr, &info, sizeof(info));
        ptr += sizeof(info);
        layerStartIndex += info.numLayers;
    }

    // Write flat layer resource ID array.
    for (Uint32 i = 0; i < numIntensities; i++) {
        for (Uint64 id : intensityLayers[i]) {
            memcpy(ptr, &id, sizeof(Uint64));
            ptr += sizeof(Uint64);
        }
    }

    cout << "Loop file " << filename << " -> " << numIntensities
         << " intensities, " << totalLayers << " total layer refs, "
         << output.size() << " bytes" << endl;
    return true;
}


int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: packer <output.pak> <file1> <file2> ... [--output-atlases] [--max-atlas-size N] [--etc]" << endl;
        cerr << "  --output-atlases: Save texture atlases as PNG files in build/ folder for review" << endl;
        cerr << "  --max-atlas-size N: Maximum atlas texture dimension (default: " << DEFAULT_ATLAS_MAX_SIZE << ")" << endl;
        cerr << "  --etc: Use ETC1/ETC2 texture compression instead of BC1/BC3 (DXT)" << endl;
        return 1;
    }

    string output;
    vector<string> inputFiles;
    bool outputAtlases = false;
    Uint32 maxAtlasSize = DEFAULT_ATLAS_MAX_SIZE;
    bool useETC = false;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--output-atlases") {
            outputAtlases = true;
        } else if (arg == "--max-atlas-size" && i + 1 < argc) {
            maxAtlasSize = (Uint32)stoul(argv[++i]);
        } else if (arg == "--etc") {
#ifdef ENABLE_ETC
            useETC = true;
#else
            cerr << "ETC compression not supported in this build" << endl;
            return 1;
#endif
        } else if (output.empty()) {
            output = arg;
        } else {
            inputFiles.push_back(arg);
        }
    }

    if (output.empty() || inputFiles.empty()) {
        cerr << "Usage: packer <output.pak> <file1> <file2> ... [--output-atlases] [--max-atlas-size N] [--etc]" << endl;
        cerr << "  --output-atlases: Save texture atlases as PNG files in build/ folder for review" << endl;
        cerr << "  --max-atlas-size N: Maximum atlas texture dimension (default: " << DEFAULT_ATLAS_MAX_SIZE << ")" << endl;
        cerr << "  --etc: Use ETC1/ETC2 texture compression instead of BC1/BC3 (DXT)" << endl;
        return 1;
    }

    vector<FileInfo> files;
    vector<PNGImageData> pngImages;  // For atlas packing

    // Collect all files and identify PNG images
    for (const string& filename : inputFiles) {
        // Skip directories
        if (!filesystem::is_regular_file(filename)) {
            continue;
        }
        string relativePath = getRelativePath(filename);
        Uint64 id = hashCString(relativePath.c_str());
        files.push_back({filename, id});
        cout << "Adding file: " << filename << " with ID " << id << endl;
    }

    // For simplicity, always rebuild when any PNG changes (atlas packing affects all images)
    // Check if pak exists and any PNG changed
    bool anyPNGChanged = false;
    bool anyOtherChanged = false;

    vector<ResourcePtr> existingPtrs;
    ifstream pakFile(output, ios::binary);
    if (pakFile) {
        PakFileHeader header;
        pakFile.read((char*)&header, sizeof(header));
        if (pakFile && memcmp(header.sig, "PAKC", 4) == 0) {
            existingPtrs.resize(header.numResources);
            pakFile.read((char*)existingPtrs.data(), sizeof(ResourcePtr) * header.numResources);

            for (auto& file : files) {
                struct stat st;
                if (stat(file.filename.c_str(), &st) != 0) {
                    file.changed = true;
                } else {
                    file.mtime = st.st_mtime;
                    bool found = false;
                    for (auto& ptr : existingPtrs) {
                        if (ptr.id == file.id) {
                            file.changed = (ptr.lastModified != (Uint64)file.mtime);
                            file.offset = ptr.offset;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        file.changed = true;
                    }
                }

                if (file.changed) {
                    Uint32 fileType = getFileType(file.filename);
                    if (fileType == RESOURCE_TYPE_IMAGE) {
                        anyPNGChanged = true;
                    } else {
                        anyOtherChanged = true;
                    }
                }
            }
        } else {
            anyPNGChanged = true;
            anyOtherChanged = true;
            for (auto& file : files) {
                file.changed = true;
            }
        }
    } else {
        anyPNGChanged = true;
        anyOtherChanged = true;
        for (auto& file : files) {
            file.changed = true;
        }
    }

    if (!anyPNGChanged && !anyOtherChanged) {
        cout << "Pak file is up to date" << endl;
        return 0;
    }

    // Rebuild
    cout << "Building pak file" << endl;

    // Check if trig table needs to be generated (only when pak doesn't exist)
    bool needTrigTable = !pakFile.is_open();
    bool hasTrigTable = false;

    // If pak exists, check if it already has a trig table
    if (pakFile.is_open()) {
        for (const auto& ptr : existingPtrs) {
            Uint64 trigTableId = hashCString("res/trig_table.bin");
            if (ptr.id == trigTableId) {
                hasTrigTable = true;
                needTrigTable = false;
                cout << "Trig lookup table already exists in pak file" << endl;
                break;
            }
        }
    }

    // If PNG files haven't changed, we need to preserve existing atlas files from the pak
    if (!anyPNGChanged && pakFile) {
        // Load existing atlas files from pak
        pakFile.seekg(sizeof(PakFileHeader));
        vector<ResourcePtr> ptrs(existingPtrs.size());
        pakFile.read((char*)ptrs.data(), sizeof(ResourcePtr) * ptrs.size());

        for (Uint64 i = 0; i < existingPtrs.size(); i++) {
            // Check if this is an atlas file by reading its header to check type
            pakFile.seekg(existingPtrs[i].offset);
            CompressionHeader comp;
            pakFile.read((char*)&comp, sizeof(comp));

            if (comp.type == RESOURCE_TYPE_IMAGE_ATLAS) {
                // This is an atlas file, add it to our files list
                FileInfo atlasFile;
                atlasFile.filename = "_atlas_" + to_string(files.size());
                atlasFile.id = existingPtrs[i].id;
                atlasFile.mtime = existingPtrs[i].lastModified;
                atlasFile.changed = false;
                atlasFile.offset = existingPtrs[i].offset;

                // Load compressed data
                atlasFile.compressedData.resize(comp.compressedSize);
                pakFile.read(atlasFile.compressedData.data(), comp.compressedSize);
                atlasFile.compressionType = comp.compressionType;
                atlasFile.decompressedSize = comp.decompressedSize;

                cout << "Preserving existing atlas with ID " << atlasFile.id << endl;
                files.push_back(std::move(atlasFile));
            } else if (comp.type == RESOURCE_TYPE_TRIG_TABLE) {
                // This is the trig table, preserve it
                FileInfo trigFile;
                trigFile.filename = "_trig_table";
                trigFile.id = existingPtrs[i].id;
                trigFile.mtime = existingPtrs[i].lastModified;
                trigFile.changed = false;
                trigFile.offset = existingPtrs[i].offset;

                // Load compressed data
                trigFile.compressedData.resize(comp.compressedSize);
                pakFile.read(trigFile.compressedData.data(), comp.compressedSize);
                trigFile.compressionType = comp.compressionType;
                trigFile.decompressedSize = comp.decompressedSize;

                cout << "Preserving existing trig lookup table with ID " << trigFile.id << endl;
                files.push_back(std::move(trigFile));
                hasTrigTable = true;
            }
        }
    }

    // Generate trig table if needed (only when pak doesn't exist)
    if (needTrigTable && !hasTrigTable) {
        cout << "Generating new trig lookup table..." << endl;
        FileInfo trigFile;
        trigFile.filename = "_trig_table";
        trigFile.id = hashCString("res/trig_table.bin");
        trigFile.mtime = time(nullptr);
        trigFile.changed = true;

        if (!generateTrigTable(trigFile.data)) {
            cerr << "Failed to generate trig lookup table" << endl;
            return 1;
        }

        compressData(trigFile.data, trigFile.compressedData, trigFile.compressionType);
        trigFile.decompressedSize = trigFile.data.size();
        cout << "Trig table size " << trigFile.decompressedSize
             << " compressed " << trigFile.compressedData.size() << endl;

        files.push_back(std::move(trigFile));
    }

    // Collect PNG images for atlas packing
    if (anyPNGChanged) {
        cout << "PNG images changed, rebuilding atlas..." << endl;
        for (auto& file : files) {
            Uint32 fileType = getFileType(file.filename);
            if (fileType == RESOURCE_TYPE_IMAGE) {
                PNGImageData imgData;
                imgData.filename = file.filename;
                imgData.id = file.id;

                struct stat st;
                if (stat(file.filename.c_str(), &st) == 0) {
                    imgData.mtime = st.st_mtime;
                    file.mtime = st.st_mtime;
                }

                if (!loadPNG(file.filename, imgData.imageData, imgData.width, imgData.height, imgData.hasAlpha)) {
                    cerr << "Failed to load PNG file " << file.filename << endl;
                    return 1;
                }
                pngImages.push_back(std::move(imgData));
            }
        }

        // Pack images into atlases
        vector<TextureAtlas> atlases;
        Uint64 numAtlases = packImagesIntoAtlases(pngImages, atlases, maxAtlasSize);
        cout << "Created " << numAtlases << " texture atlas(es)" << endl;

        // Print atlas info
        for (Uint64 i = 0; i < atlases.size(); i++) {
            const char* fmtName = useETC
                ? (atlases[i].hasAlpha ? "RGBA/ETC2" : "RGB/ETC1")
                : (atlases[i].hasAlpha ? "RGBA/BC3"  : "RGB/BC1");
            cout << "  Atlas " << i << ": " << atlases[i].width << "x" << atlases[i].height
                 << " (" << atlases[i].entries.size() << " images, "
                 << fmtName << ")" << endl;
        }

        // Save atlases as PNG files if requested
        if (outputAtlases) {
            for (Uint64 i = 0; i < atlases.size(); i++) {
                string atlasFilename = "atlas_" + to_string(i) + ".png";
                if (savePNG(atlasFilename, atlases[i].imageData, atlases[i].width, atlases[i].height)) {
                    cout << "Saved atlas " << i << " as " << atlasFilename << endl;
                } else {
                    cerr << "Failed to save atlas " << i << " as " << atlasFilename << endl;
                }
            }
        }

        // Process atlases and update file data for images
        // First, create a map from original image ID to atlas info
        struct AtlasInfo {
            Uint64 atlasIndex;
            AtlasEntry entry;
            bool isPacked;  // Whether this image was packed into an atlas
        };
        vector<AtlasInfo> imageToAtlas(pngImages.size());
        for (Uint64 i = 0; i < imageToAtlas.size(); i++) {
            imageToAtlas[i].isPacked = false;
        }

        for (Uint64 atlasIdx = 0; atlasIdx < atlases.size(); atlasIdx++) {
            TextureAtlas& atlas = atlases[atlasIdx];
            for (Uint64 i = 0; i < atlas.packedImageIndices.size(); i++) {
                Uint64 imgIdx = atlas.packedImageIndices[i];
                imageToAtlas[imgIdx].atlasIndex = atlasIdx;
                imageToAtlas[imgIdx].entry = atlas.entries[i];
                imageToAtlas[imgIdx].isPacked = true;
            }
        }

        // For each image file, either create a TextureHeader (if packed) or ImageHeader (if standalone)
        for (Uint64 i = 0; i < pngImages.size(); i++) {
            PNGImageData& img = pngImages[i];
            AtlasInfo& atlasInfo = imageToAtlas[i];

            if (!atlasInfo.isPacked) {
                // Image was too large for atlas - store as standalone ImageHeader
                cout << "Image " << img.filename << " too large for atlas, storing standalone" << endl;

                vector<char> compressedImage;
                Uint16 format;
                compressImageRaw(img.imageData, compressedImage, img.width, img.height, img.hasAlpha, format, useETC);

                // Create ImageHeader
                ImageHeader header;
                header.format = format;
                header.width = img.width;
                header.height = img.height;
                header.pad = 0;

                // Find corresponding file and set its data
                for (auto& file : files) {
                    if (file.id == img.id) {
                        file.data.resize(sizeof(ImageHeader) + compressedImage.size());
                        memcpy(file.data.data(), &header, sizeof(ImageHeader));
                        memcpy(file.data.data() + sizeof(ImageHeader), compressedImage.data(), compressedImage.size());
                        file.changed = true;
                        break;
                    }
                }
                continue;
            }

            TextureAtlas& atlas = atlases[atlasInfo.atlasIndex];
            AtlasEntry& entry = atlasInfo.entry;

            // Calculate UV coordinates
            // u0,v0 = top-left corner in texture space (V increases downward in image)
            // u1,v1 = bottom-right corner in texture space
            float u0 = (float)entry.x / atlas.width;
            float v0 = (float)entry.y / atlas.height;
            float u1 = (float)(entry.x + entry.width) / atlas.width;
            float v1 = (float)(entry.y + entry.height) / atlas.height;

            // Create TextureHeader
            // UV coordinate layout in coordinates[8] array:
            //   [0,1] = bottom-left  (u0, v1)
            //   [2,3] = bottom-right (u1, v1)
            //   [4,5] = top-right    (u1, v0)
            //   [6,7] = top-left     (u0, v0)
            // Note: v is stored with bottom having v1 (higher value) because
            // image Y increases downward but GPU texture V typically increases upward
            TextureHeader texHeader;
            texHeader.atlasId = atlas.atlasId;
            texHeader.coordinates[0] = u0;  // bottom-left u
            texHeader.coordinates[1] = v1;  // bottom-left v
            texHeader.coordinates[2] = u1;  // bottom-right u
            texHeader.coordinates[3] = v1;  // bottom-right v
            texHeader.coordinates[4] = u1;  // top-right u
            texHeader.coordinates[5] = v0;  // top-right v
            texHeader.coordinates[6] = u0;  // top-left u
            texHeader.coordinates[7] = v0;  // top-left v

            // Find corresponding file and set its data
            for (auto& file : files) {
                if (file.id == img.id) {
                    file.data.resize(sizeof(TextureHeader));
                    memcpy(file.data.data(), &texHeader, sizeof(TextureHeader));
                    file.changed = true;  // Mark as changed for atlas images
                    break;
                }
            }
        }

        // Now add atlas files as new resources
        for (Uint64 i = 0; i < atlases.size(); i++) {
            TextureAtlas& atlas = atlases[i];
            FileInfo atlasFile;
            atlasFile.filename = "_atlas_" + to_string(i);
            atlasFile.id = atlas.atlasId;
            atlasFile.mtime = time(nullptr);  // Current time
            atlasFile.changed = true;

            if (!processAtlas(atlas, atlasFile.data, useETC)) {
                cerr << "Failed to process atlas " << i << endl;
                return 1;
            }

            compressData(atlasFile.data, atlasFile.compressedData, atlasFile.compressionType);
            atlasFile.decompressedSize = atlasFile.data.size();
            cout << "Atlas " << i << " size " << atlasFile.decompressedSize
                 << " compressed " << atlasFile.compressedData.size() << endl;

            files.push_back(std::move(atlasFile));
        }
    }

    // Process non-image files
    for (auto& file : files) {
        Uint32 fileType = getFileType(file.filename);

        if (fileType == RESOURCE_TYPE_IMAGE) {
            if (anyPNGChanged) {
                // Data was already set during atlas packing
                if (file.data.empty()) {
                    // This shouldn't happen, but handle gracefully
                    cerr << "Warning: Empty data for image " << file.filename << endl;
                }
                compressData(file.data, file.compressedData, file.compressionType);
                file.decompressedSize = file.data.size();
                cout << "File " << file.filename << " (atlas reference) size " << file.decompressedSize
                     << " compressed " << file.compressedData.size() << endl;
            } else {
                // No PNG changed, load from existing pak file
                pakFile.seekg(file.offset);
                CompressionHeader comp;
                pakFile.read((char*)&comp, sizeof(comp));
                file.compressedData.resize(comp.compressedSize);
                pakFile.read(file.compressedData.data(), comp.compressedSize);
                file.compressionType = comp.compressionType;
                file.decompressedSize = comp.decompressedSize;
                cout << "File " << file.filename << " (atlas reference) unchanged, using cached data" << endl;
            }
        } else if (file.filename.find("_atlas_") == 0) {
            // Atlas file, already processed
            continue;
        } else if (file.filename == "_trig_table") {
            // Trig table, already processed
            continue;
        } else if (file.changed) {
            // Standard file processing
            Uint32 ft = getFileType(file.filename);
            if (ft == RESOURCE_TYPE_MUSIC_TRACK) {
                // .loop JSON -> binary MusicTrackHeader
                if (!processLoopFile(file.filename, file.data)) {
                    cerr << "Failed to process loop file " << file.filename << endl;
                    return 1;
                }
            } else {
                if (!loadFile(file.filename, file.data, file.mtime)) {
                    cerr << "Failed to load " << file.filename << endl;
                    return 1;
                }
            }
            compressData(file.data, file.compressedData, file.compressionType);
            file.decompressedSize = file.data.size();
            cout << "File " << file.filename << " original " << file.decompressedSize
                 << " compressed " << file.compressedData.size()
                 << " type " << fileType << endl;
        } else {
            // Load from existing pak
            pakFile.seekg(file.offset);
            CompressionHeader comp;
            pakFile.read((char*)&comp, sizeof(comp));
            file.compressedData.resize(comp.compressedSize);
            pakFile.read(file.compressedData.data(), comp.compressedSize);
            file.compressionType = comp.compressionType;
            file.decompressedSize = comp.decompressedSize;
            cout << "File " << file.filename << " unchanged, using cached data" << endl;
        }
    }

    // Write output pak file
    ofstream out(output, ios::binary);
    PakFileHeader header;
    memcpy(header.sig, "PAKC", 4);
    header.version = VERSION_1_0;
    header.numResources = files.size();
    header.pad = 0;
    out.write((char*)&header, sizeof(header));

    Uint64 offset = sizeof(header) + sizeof(ResourcePtr) * files.size();
    for (auto& file : files) {
        ResourcePtr ptr = {file.id, offset, (Uint64)file.mtime};
        out.write((char*)&ptr, sizeof(ptr));
        offset += sizeof(CompressionHeader) + file.compressedData.size();
    }

    for (auto& file : files) {
        // Determine file type
        Uint32 fileType;
        if (file.filename.find("_atlas_") == 0) {
            fileType = RESOURCE_TYPE_IMAGE_ATLAS;
        } else if (file.filename == "_trig_table") {
            fileType = RESOURCE_TYPE_TRIG_TABLE;
        } else {
            Uint32 origType = getFileType(file.filename);
            // Image files are now texture headers referencing atlases
            fileType = origType;
        }

        CompressionHeader comp = {file.compressionType, (Uint32)file.compressedData.size(),
                                  file.decompressedSize, fileType};
        out.write((char*)&comp, sizeof(comp));
        out.write(file.compressedData.data(), file.compressedData.size());
    }

    cout << "Pak file created with " << files.size() << " resources" << endl;
    return 0;
}