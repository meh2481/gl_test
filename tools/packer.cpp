#include "../src/ResourceTypes.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <sys/stat.h>
#include <cstring>
#include <cmath>
#include <lz4.h>
#include <filesystem>
#include <functional>
#include <png.h>
#include <squish.h>
#include <cassert>
#include <algorithm>

using namespace std;

struct FileInfo {
    string filename;
    uint64_t id;
    time_t mtime;
    vector<char> data;
    vector<char> compressedData;
    uint32_t compressionType;
    uint32_t decompressedSize;
    bool changed;
    uint64_t offset; // for unchanged files
};

// Structure for raw PNG image data before atlas packing
struct PNGImageData {
    string filename;
    uint64_t id;
    time_t mtime;
    vector<uint8_t> imageData;  // RGBA data
    uint32_t width;
    uint32_t height;
    bool hasAlpha;
};

// Rectangle for bin packing
struct PackRect {
    uint32_t width;
    uint32_t height;
    uint32_t x;
    uint32_t y;
    size_t imageIndex;  // Index into PNGImageData array
    bool packed;
};

// Simple maxrects bin packing algorithm
class MaxRectsBinPacker {
public:
    MaxRectsBinPacker(uint32_t binWidth, uint32_t binHeight)
        : binWidth_(binWidth), binHeight_(binHeight) {
        // Start with one free rect covering entire bin
        freeRects_.push_back({0, 0, binWidth, binHeight});
    }

    // Try to pack a rectangle, returns true if successful
    bool pack(PackRect& rect) {
        int bestIndex = -1;
        uint32_t bestShortSide = UINT32_MAX;

        // Find best free rect using Best Short Side Fit heuristic
        for (size_t i = 0; i < freeRects_.size(); i++) {
            FreeRect& freeRect = freeRects_[i];

            if (rect.width <= freeRect.width && rect.height <= freeRect.height) {
                uint32_t shortSide = min(freeRect.width - rect.width, freeRect.height - rect.height);
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
        uint32_t x, y, width, height;
    };

    vector<FreeRect> freeRects_;
    uint32_t binWidth_;
    uint32_t binHeight_;

    void splitFreeRect(int index, const PackRect& usedRect) {
        FreeRect freeRect = freeRects_[index];
        freeRects_.erase(freeRects_.begin() + index);

        // Split horizontally (right remainder)
        if (usedRect.x + usedRect.width < freeRect.x + freeRect.width) {
            FreeRect newRect;
            newRect.x = usedRect.x + usedRect.width;
            newRect.y = freeRect.y;
            newRect.width = freeRect.x + freeRect.width - newRect.x;
            newRect.height = freeRect.height;
            freeRects_.push_back(newRect);
        }

        // Split vertically (bottom remainder)
        if (usedRect.y + usedRect.height < freeRect.y + freeRect.height) {
            FreeRect newRect;
            newRect.x = freeRect.x;
            newRect.y = usedRect.y + usedRect.height;
            newRect.width = freeRect.width;
            newRect.height = freeRect.y + freeRect.height - newRect.y;
            freeRects_.push_back(newRect);
        }

        // Merge overlapping free rects and remove fully contained ones
        pruneFreeRects();
    }

    void pruneFreeRects() {
        // Remove any free rects fully contained within another
        for (size_t i = 0; i < freeRects_.size(); i++) {
            for (size_t j = i + 1; j < freeRects_.size(); ) {
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
    uint64_t atlasId;
    uint32_t width;
    uint32_t height;
    bool hasAlpha;
    vector<uint8_t> imageData;  // RGBA atlas image
    vector<AtlasEntry> entries;
    vector<size_t> packedImageIndices;  // Indices of images packed into this atlas
};

bool loadFile(const string& filename, vector<char>& data, time_t& mtime) {
    struct stat st;
    if (stat(filename.c_str(), &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;  // Skip directories and special files
    mtime = st.st_mtime;
    ifstream file(filename, ios::binary | ios::ate);
    if (!file) return false;
    size_t size = file.tellg();
    file.seekg(0);
    data.resize(size);
    file.read(data.data(), size);
    return true;
}

void compressData(const vector<char>& input, vector<char>& output, uint32_t& compressionType) {
    int maxCompressedSize = LZ4_compressBound(input.size());
    output.resize(maxCompressedSize);
    int compressedSize = LZ4_compress_default(input.data(), output.data(), input.size(), maxCompressedSize);
    if (compressedSize > 0 && compressedSize < (int)input.size()) {
        output.resize(compressedSize);
        compressionType = COMPRESSION_FLAGS_LZ4;
    } else {
        // Compression didn't help, store uncompressed
        output = input;
        compressionType = COMPRESSION_FLAGS_UNCOMPRESSED;
    }
}

uint32_t getFileType(const string& filename, bool isAtlas = false) {
    if (isAtlas) return RESOURCE_TYPE_IMAGE_ATLAS;
    string ext = filesystem::path(filename).extension().string();
    if (ext == ".lua") return RESOURCE_TYPE_LUA;
    if (ext == ".spv") return RESOURCE_TYPE_SHADER;
    if (ext == ".png") return RESOURCE_TYPE_IMAGE;
    if (ext == ".opus") return RESOURCE_TYPE_SOUND;
    // Add more extensions as needed
    return RESOURCE_TYPE_UNKNOWN;
}

// Round up to next multiple of 4 for DXT block alignment
uint32_t alignTo4(uint32_t val) {
    return (val + 3) & ~3;
}

// Load PNG image and convert to RGBA
bool loadPNG(const string& filename, vector<uint8_t>& imageData, uint32_t& width, uint32_t& height, bool& hasAlpha) {
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
    for (uint32_t y = 0; y < height; y++) {
        row_pointers[y] = imageData.data() + y * width * bytesPerPixel;
    }

    png_read_image(png, row_pointers.data());
    png_read_end(png, nullptr);

    png_destroy_read_struct(&png, &info, nullptr);
    fclose(fp);

    return true;
}

// Compress image data using DXT/BC compression
void compressImage(const vector<uint8_t>& imageData, vector<char>& compressed, uint32_t width, uint32_t height, bool hasAlpha, uint16_t& format) {
    assert(width > 0 && height > 0);
    // imageData is always RGBA (4 bytes per pixel) at this point
    assert(imageData.size() == width * height * 4);

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
void compressImageRaw(const vector<uint8_t>& imageData, vector<char>& compressed, uint32_t width, uint32_t height, bool hasAlpha, uint16_t& format) {
    assert(width > 0 && height > 0);
    int bytesPerPixel = hasAlpha ? 4 : 3;
    assert(imageData.size() == width * height * bytesPerPixel);

    // Convert to RGBA if needed
    vector<uint8_t> rgbaData;
    if (!hasAlpha) {
        rgbaData.resize(width * height * 4);
        for (uint32_t i = 0; i < width * height; i++) {
            rgbaData[i * 4 + 0] = imageData[i * 3 + 0];  // R
            rgbaData[i * 4 + 1] = imageData[i * 3 + 1];  // G
            rgbaData[i * 4 + 2] = imageData[i * 3 + 2];  // B
            rgbaData[i * 4 + 3] = 255;                   // A (fully opaque)
        }
        compressImage(rgbaData, compressed, width, height, false, format);
    } else {
        compressImage(imageData, compressed, width, height, true, format);
    }
}

// Pack multiple PNG images into texture atlases
// Returns the number of atlases created
size_t packImagesIntoAtlases(vector<PNGImageData>& images, vector<TextureAtlas>& atlases, uint32_t maxAtlasSize) {
    if (images.empty()) return 0;

    // Sort all images by area (descending) for better bin packing
    // Larger images should be placed first
    vector<size_t> sortedIndices(images.size());
    for (size_t i = 0; i < images.size(); i++) {
        sortedIndices[i] = i;
    }
    sort(sortedIndices.begin(), sortedIndices.end(), [&images](size_t a, size_t b) {
        uint32_t areaA = images[a].width * images[a].height;
        uint32_t areaB = images[b].width * images[b].height;
        return areaA > areaB;
    });

    // Prepare rectangles for all images
    // Add 2 pixels (1 on each side) for edge padding to prevent texture bleeding
    const uint32_t EDGE_PADDING = 1;
    vector<PackRect> rects;
    rects.reserve(images.size());
    for (size_t idx : sortedIndices) {
        PackRect rect;
        // Add padding on each side, then align to 4 pixels for DXT block boundaries
        rect.width = alignTo4(images[idx].width + EDGE_PADDING * 2);
        rect.height = alignTo4(images[idx].height + EDGE_PADDING * 2);
        rect.imageIndex = idx;
        rect.packed = false;
        rects.push_back(rect);
    }

    // Calculate total area needed (with some padding for inefficiency)
    uint64_t totalArea = 0;
    uint32_t maxImageW = 0, maxImageH = 0;
    for (const auto& rect : rects) {
        totalArea += (uint64_t)rect.width * rect.height;
        maxImageW = max(maxImageW, rect.width);
        maxImageH = max(maxImageH, rect.height);
    }

    // Start with atlas size that can fit all images (estimate with 30% overhead)
    uint32_t estimatedSize = (uint32_t)sqrt((double)totalArea * 1.3);
    uint32_t atlasWidth = 256;
    uint32_t atlasHeight = 256;
    while (atlasWidth < estimatedSize && atlasWidth < maxAtlasSize) atlasWidth *= 2;
    while (atlasHeight < estimatedSize && atlasHeight < maxAtlasSize) atlasHeight *= 2;
    // Ensure atlas is at least as large as the biggest image
    while (atlasWidth < maxImageW && atlasWidth < maxAtlasSize) atlasWidth *= 2;
    while (atlasHeight < maxImageH && atlasHeight < maxAtlasSize) atlasHeight *= 2;

    // Try to pack all images into atlases
    while (true) {
        // Find unpacked rects
        vector<size_t> unpacked;
        for (size_t i = 0; i < rects.size(); i++) {
            if (!rects[i].packed) {
                unpacked.push_back(i);
            }
        }
        if (unpacked.empty()) break;

        // Try progressively larger atlas sizes until we can pack at least one image
        uint32_t tryWidth = atlasWidth;
        uint32_t tryHeight = atlasHeight;
        bool success = false;
        vector<size_t> packedInThisAtlas;

        while (!success && tryWidth <= maxAtlasSize && tryHeight <= maxAtlasSize) {
            MaxRectsBinPacker packer(tryWidth, tryHeight);
            packedInThisAtlas.clear();

            // Try to pack all unpacked images into this atlas
            for (size_t i : unpacked) {
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
            for (size_t i : unpacked) {
                rects[i].packed = true;  // Mark to avoid infinite loop
            }
            continue;
        }

        // Determine if this atlas needs alpha (BC3) or can use BC1
        bool atlasHasAlpha = false;
        for (size_t i : packedInThisAtlas) {
            if (images[rects[i].imageIndex].hasAlpha) {
                atlasHasAlpha = true;
                break;
            }
        }

        // Generate deterministic atlas ID based on content
        uint64_t atlasContentHash = 0;
        for (size_t i : packedInThisAtlas) {
            atlasContentHash ^= images[rects[i].imageIndex].id;
            atlasContentHash = (atlasContentHash << 7) | (atlasContentHash >> 57);
        }
        atlasContentHash ^= ((uint64_t)tryWidth << 32) | tryHeight;
        atlasContentHash ^= atlasHasAlpha ? 0xFFFFFFFF : 0;

        // Create the atlas
        TextureAtlas atlas;
        atlas.atlasId = atlasContentHash;
        atlas.width = tryWidth;
        atlas.height = tryHeight;
        atlas.hasAlpha = atlasHasAlpha;
        atlas.imageData.resize(tryWidth * tryHeight * 4);
        // Initialize to hot pink (255, 0, 255, 255)
        for (size_t i = 0; i < atlas.imageData.size(); i += 4) {
            atlas.imageData[i] = 255;     // R
            atlas.imageData[i + 1] = 0;   // G
            atlas.imageData[i + 2] = 255; // B
            atlas.imageData[i + 3] = 255; // A
        }

        // Copy packed images into atlas with edge padding
        const uint32_t EDGE_PADDING = 1;
        for (size_t i : packedInThisAtlas) {
            PackRect& rect = rects[i];
            PNGImageData& img = images[rect.imageIndex];

            // Convert source image to RGBA if needed
            vector<uint8_t> rgbaSource;
            const uint8_t* srcData;
            if (img.hasAlpha) {
                srcData = img.imageData.data();
            } else {
                rgbaSource.resize(img.width * img.height * 4);
                for (uint32_t p = 0; p < img.width * img.height; p++) {
                    rgbaSource[p * 4 + 0] = img.imageData[p * 3 + 0];
                    rgbaSource[p * 4 + 1] = img.imageData[p * 3 + 1];
                    rgbaSource[p * 4 + 2] = img.imageData[p * 3 + 2];
                    rgbaSource[p * 4 + 3] = 255;
                }
                srcData = rgbaSource.data();
            }

            // Helper lambda to copy a pixel from source to atlas
            auto copyPixel = [&](uint32_t srcX, uint32_t srcY, uint32_t dstX, uint32_t dstY) {
                // Clamp source coordinates to valid range
                srcX = min(srcX, img.width - 1);
                srcY = min(srcY, img.height - 1);
                uint32_t srcIdx = (srcY * img.width + srcX) * 4;
                uint32_t dstIdx = (dstY * tryWidth + dstX) * 4;
                atlas.imageData[dstIdx + 0] = srcData[srcIdx + 0];
                atlas.imageData[dstIdx + 1] = srcData[srcIdx + 1];
                atlas.imageData[dstIdx + 2] = srcData[srcIdx + 2];
                atlas.imageData[dstIdx + 3] = srcData[srcIdx + 3];
            };

            // The actual image content starts at (rect.x + EDGE_PADDING, rect.y + EDGE_PADDING)
            uint32_t contentX = rect.x + EDGE_PADDING;
            uint32_t contentY = rect.y + EDGE_PADDING;

            // Copy main image content
            for (uint32_t y = 0; y < img.height; y++) {
                for (uint32_t x = 0; x < img.width; x++) {
                    copyPixel(x, y, contentX + x, contentY + y);
                }
            }

            // Duplicate edge pixels for all padding rows/columns (prevents texture bleeding with MSAA)
            for (uint32_t p = 1; p <= EDGE_PADDING; p++) {
                // Top edge padding (duplicate first row)
                for (uint32_t x = 0; x < img.width; x++) {
                    copyPixel(x, 0, contentX + x, contentY - p);
                }
                // Bottom edge padding (duplicate last row)
                for (uint32_t x = 0; x < img.width; x++) {
                    copyPixel(x, img.height - 1, contentX + x, contentY + img.height - 1 + p);
                }
                // Left edge padding (duplicate first column)
                for (uint32_t y = 0; y < img.height; y++) {
                    copyPixel(0, y, contentX - p, contentY + y);
                }
                // Right edge padding (duplicate last column)
                for (uint32_t y = 0; y < img.height; y++) {
                    copyPixel(img.width - 1, y, contentX + img.width - 1 + p, contentY + y);
                }
            }

            // Corner padding - fill all corner pixels with edge pixel values
            for (uint32_t py = 1; py <= EDGE_PADDING; py++) {
                for (uint32_t px = 1; px <= EDGE_PADDING; px++) {
                    copyPixel(0, 0, contentX - px, contentY - py);  // Top-left
                    copyPixel(img.width - 1, 0, contentX + img.width - 1 + px, contentY - py);  // Top-right
                    copyPixel(0, img.height - 1, contentX - px, contentY + img.height - 1 + py);  // Bottom-left
                    copyPixel(img.width - 1, img.height - 1, contentX + img.width - 1 + px, contentY + img.height - 1 + py);  // Bottom-right
                }
            }

            // Create atlas entry - point to actual content (offset by EDGE_PADDING)
            AtlasEntry entry;
            entry.originalId = img.id;
            entry.x = (uint16_t)contentX;
            entry.y = (uint16_t)contentY;
            entry.width = (uint16_t)img.width;
            entry.height = (uint16_t)img.height;
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
bool processAtlas(TextureAtlas& atlas, vector<char>& output) {
    assert(atlas.width > 0 && atlas.height > 0);
    assert(atlas.imageData.size() == atlas.width * atlas.height * 4);

    // Compress the atlas image data
    vector<char> compressedImage;
    uint16_t format;
    compressImage(atlas.imageData, compressedImage, atlas.width, atlas.height, atlas.hasAlpha, format);

    assert(compressedImage.size() > 0);

    // Create AtlasHeader
    AtlasHeader header;
    header.format = format;
    header.width = (uint16_t)atlas.width;
    header.height = (uint16_t)atlas.height;
    header.numEntries = (uint16_t)atlas.entries.size();

    // Calculate total size: header + entries + compressed data
    size_t entriesSize = sizeof(AtlasEntry) * atlas.entries.size();
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
bool processPNGFile(const string& filename, vector<char>& output) {
    vector<uint8_t> imageData;
    uint32_t width, height;
    bool hasAlpha;

    if (!loadPNG(filename, imageData, width, height, hasAlpha)) {
        return false;
    }

    assert(width > 0 && height > 0);
    int bytesPerPixel = hasAlpha ? 4 : 3;
    assert(imageData.size() == width * height * bytesPerPixel);

    // Compress the image data
    vector<char> compressedImage;
    uint16_t format;
    compressImageRaw(imageData, compressedImage, width, height, hasAlpha, format);

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

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: packer <output.pak> <file1> <file2> ..." << endl;
        return 1;
    }

    string output = argv[1];
    vector<FileInfo> files;
    vector<PNGImageData> pngImages;  // For atlas packing

    // Collect all files and identify PNG images
    for (int i = 2; i < argc; ++i) {
        string filename = argv[i];
        // Skip directories
        if (!filesystem::is_regular_file(filename)) {
            continue;
        }
        string basename = filesystem::path(filename).filename().string();
        uint64_t id = hash<string>{}(basename);
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
                            file.changed = (ptr.lastModified != (uint64_t)file.mtime);
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
                    uint32_t fileType = getFileType(file.filename);
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

    // If PNG files haven't changed, we need to preserve existing atlas files from the pak
    if (!anyPNGChanged && pakFile) {
        // Load existing atlas files from pak
        pakFile.seekg(sizeof(PakFileHeader));
        vector<ResourcePtr> ptrs(existingPtrs.size());
        pakFile.read((char*)ptrs.data(), sizeof(ResourcePtr) * ptrs.size());

        for (size_t i = 0; i < existingPtrs.size(); i++) {
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
            }
        }
    }

    // Collect PNG images for atlas packing
    if (anyPNGChanged) {
        cout << "PNG images changed, rebuilding atlas..." << endl;
        for (auto& file : files) {
            uint32_t fileType = getFileType(file.filename);
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
        size_t numAtlases = packImagesIntoAtlases(pngImages, atlases, DEFAULT_ATLAS_MAX_SIZE);
        cout << "Created " << numAtlases << " texture atlas(es)" << endl;

        // Print atlas info
        for (size_t i = 0; i < atlases.size(); i++) {
            cout << "  Atlas " << i << ": " << atlases[i].width << "x" << atlases[i].height
                 << " (" << atlases[i].entries.size() << " images, "
                 << (atlases[i].hasAlpha ? "RGBA/BC3" : "RGB/BC1") << ")" << endl;
        }

        // Process atlases and update file data for images
        // First, create a map from original image ID to atlas info
        struct AtlasInfo {
            size_t atlasIndex;
            AtlasEntry entry;
            bool isPacked;  // Whether this image was packed into an atlas
        };
        vector<AtlasInfo> imageToAtlas(pngImages.size());
        for (size_t i = 0; i < imageToAtlas.size(); i++) {
            imageToAtlas[i].isPacked = false;
        }

        for (size_t atlasIdx = 0; atlasIdx < atlases.size(); atlasIdx++) {
            TextureAtlas& atlas = atlases[atlasIdx];
            for (size_t i = 0; i < atlas.packedImageIndices.size(); i++) {
                size_t imgIdx = atlas.packedImageIndices[i];
                imageToAtlas[imgIdx].atlasIndex = atlasIdx;
                imageToAtlas[imgIdx].entry = atlas.entries[i];
                imageToAtlas[imgIdx].isPacked = true;
            }
        }

        // For each image file, either create a TextureHeader (if packed) or ImageHeader (if standalone)
        for (size_t i = 0; i < pngImages.size(); i++) {
            PNGImageData& img = pngImages[i];
            AtlasInfo& atlasInfo = imageToAtlas[i];

            if (!atlasInfo.isPacked) {
                // Image was too large for atlas - store as standalone ImageHeader
                cout << "Image " << img.filename << " too large for atlas, storing standalone" << endl;

                vector<char> compressedImage;
                uint16_t format;
                compressImageRaw(img.imageData, compressedImage, img.width, img.height, img.hasAlpha, format);

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
        for (size_t i = 0; i < atlases.size(); i++) {
            TextureAtlas& atlas = atlases[i];
            FileInfo atlasFile;
            atlasFile.filename = "_atlas_" + to_string(i);
            atlasFile.id = atlas.atlasId;
            atlasFile.mtime = time(nullptr);  // Current time
            atlasFile.changed = true;

            if (!processAtlas(atlas, atlasFile.data)) {
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
        uint32_t fileType = getFileType(file.filename);

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
        } else if (file.changed) {
            // Standard file processing
            if (!loadFile(file.filename, file.data, file.mtime)) {
                cerr << "Failed to load " << file.filename << endl;
                return 1;
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

    uint64_t offset = sizeof(header) + sizeof(ResourcePtr) * files.size();
    for (auto& file : files) {
        ResourcePtr ptr = {file.id, offset, (uint64_t)file.mtime};
        out.write((char*)&ptr, sizeof(ptr));
        offset += sizeof(CompressionHeader) + file.compressedData.size();
    }

    for (auto& file : files) {
        // Determine file type
        uint32_t fileType;
        if (file.filename.find("_atlas_") == 0) {
            fileType = RESOURCE_TYPE_IMAGE_ATLAS;
        } else {
            uint32_t origType = getFileType(file.filename);
            // Image files are now texture headers referencing atlases
            fileType = origType;
        }

        CompressionHeader comp = {file.compressionType, (uint32_t)file.compressedData.size(),
                                  (uint32_t)file.data.size(), fileType};
        out.write((char*)&comp, sizeof(comp));
        out.write(file.compressedData.data(), file.compressedData.size());
    }

    cout << "Pak file created with " << files.size() << " resources" << endl;
    return 0;
}