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

// nanosvg: vendor-included SVG parser (packer-only, no runtime linkage)
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

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

// ============================================================================
// IMA ADPCM encoder (for WAV -> GLA conversion)
// Produces data in the AL_EXT_IMA4 block format:
//   Mono   block: 36 bytes (4-byte header + 32 bytes nibbles) = 65 samples/block
//   Stereo block: 72 bytes (8-byte header + 64 bytes interleaved nibbles) = 65 samples/channel/block
// ============================================================================

static const int IMA_STEP_TABLE[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static const int IMA_INDEX_TABLE[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

// Encode one PCM sample as a 4-bit IMA ADPCM nibble.
// predictor and stepIndex are updated in-place (mirror the decoder state).
static int imaEncodeNibble(int sample, int* predictor, int* stepIndex) {
    assert(stepIndex != nullptr);
    assert(predictor != nullptr);
    assert(*stepIndex >= 0 && *stepIndex <= 88);

    int step = IMA_STEP_TABLE[*stepIndex];
    int diff = sample - *predictor;
    int nibble = 0;

    if (diff < 0) {
        nibble = 8;
        diff = -diff;
    }

    if (diff >= step)       { nibble |= 4; diff -= step; }
    step >>= 1;
    if (diff >= step)       { nibble |= 2; diff -= step; }
    step >>= 1;
    if (diff >= step)       { nibble |= 1; }

    // Update predictor using the same arithmetic as the decoder so round-trips match.
    int fullStep = IMA_STEP_TABLE[*stepIndex];
    int vpdiff   = fullStep >> 3;
    if (nibble & 4) vpdiff += fullStep;
    if (nibble & 2) vpdiff += fullStep >> 1;
    if (nibble & 1) vpdiff += fullStep >> 2;

    if (nibble & 8) *predictor -= vpdiff;
    else            *predictor += vpdiff;

    if (*predictor >  32767) *predictor =  32767;
    if (*predictor < -32768) *predictor = -32768;

    *stepIndex += IMA_INDEX_TABLE[nibble];
    if (*stepIndex <  0) *stepIndex = 0;
    if (*stepIndex > 88) *stepIndex = 88;

    return nibble & 0xF;
}

// Read a 16-bit PCM WAV file from disk.
// Fills samples (interleaved L/R for stereo), channels, and sampleRate.
// Returns false on error.
static bool readWavFile(const string& filename,
                        vector<Sint16>& samples,
                        int& channels,
                        int& sampleRate)
{
    ifstream f(filename, ios::binary);
    if (!f) {
        cerr << "readWavFile: cannot open '" << filename << "'" << endl;
        return false;
    }

    // RIFF header
    char riff[4];
    f.read(riff, 4);
    if (memcmp(riff, "RIFF", 4) != 0) {
        cerr << "readWavFile: not a RIFF file: " << filename << endl;
        return false;
    }
    Uint32 riffSize = 0;
    f.read(reinterpret_cast<char*>(&riffSize), 4);
    char wave[4];
    f.read(wave, 4);
    if (memcmp(wave, "WAVE", 4) != 0) {
        cerr << "readWavFile: not a WAVE file: " << filename << endl;
        return false;
    }

    channels    = 0;
    sampleRate  = 0;
    Uint32 dataSize    = 0;
    streampos  dataOffset = 0;

    // Parse chunks until we find fmt  and data.
    while (f && f.good()) {
        char chunkId[4];
        f.read(chunkId, 4);
        if (f.gcount() < 4) break;

        Uint32 chunkSize = 0;
        f.read(reinterpret_cast<char*>(&chunkSize), 4);
        if (f.gcount() < 4) break;

        if (memcmp(chunkId, "fmt ", 4) == 0) {
            Uint16 audioFormat = 0, ch = 0, blockAlign = 0, bps = 0;
            Uint32 sr = 0, byteRate = 0;
            f.read(reinterpret_cast<char*>(&audioFormat), 2);
            f.read(reinterpret_cast<char*>(&ch),          2);
            f.read(reinterpret_cast<char*>(&sr),           4);
            f.read(reinterpret_cast<char*>(&byteRate),     4);
            f.read(reinterpret_cast<char*>(&blockAlign),   2);
            f.read(reinterpret_cast<char*>(&bps),          2);

            if (audioFormat != 1) {
                cerr << "readWavFile: only PCM (format 1) supported, got "
                     << audioFormat << " in " << filename << endl;
                return false;
            }
            if (bps != 16) {
                cerr << "readWavFile: only 16-bit samples supported, got "
                     << bps << " bits in " << filename << endl;
                return false;
            }
            if (ch != 1 && ch != 2) {
                cerr << "readWavFile: only mono/stereo supported, got "
                     << ch << " channels in " << filename << endl;
                return false;
            }
            channels   = (int)ch;
            sampleRate = (int)sr;

            // Skip any extra fmt bytes (e.g. PCM extension words)
            if (chunkSize > 16) {
                f.seekg(chunkSize - 16, ios::cur);
            }

        } else if (memcmp(chunkId, "data", 4) == 0) {
            dataSize   = chunkSize;
            dataOffset = f.tellg();
            break; // data chunk found — stop scanning

        } else {
            // Unknown chunk — skip it
            f.seekg(chunkSize, ios::cur);
        }
    }

    if (channels == 0 || sampleRate == 0 || dataSize == 0) {
        cerr << "readWavFile: missing fmt or data chunk in " << filename << endl;
        return false;
    }

    f.seekg(dataOffset);
    size_t numSamples = dataSize / sizeof(Sint16);
    samples.resize(numSamples);
    f.read(reinterpret_cast<char*>(samples.data()), dataSize);
    if (!f && !f.eof()) {
        cerr << "readWavFile: read error in " << filename << endl;
        return false;
    }

    return true;
}

// Encode a 16-bit PCM WAV file to the GLA format (IMA ADPCM + GlaHeader).
// Returns false on error.
static bool processWavToGla(const string& filename, vector<char>& output) {
    vector<Sint16> samples;
    int channels = 0, sampleRate = 0;

    if (!readWavFile(filename, samples, channels, sampleRate)) {
        return false;
    }

    assert(channels >= 1 && channels <= 2);
    assert(sampleRate > 0);

    size_t totalSamples = samples.size() / (size_t)channels; // per-channel count
    assert(totalSamples > 0);

    // Fixed IMA4 block parameters for AL_EXT_IMA4 default block alignment (65 samples/block).
    const int BLOCK_BYTES = (channels == 1) ? GLA_MONO_BLOCK_BYTES : GLA_STEREO_BLOCK_BYTES;
    const int SPB         = GLA_SAMPLES_PER_BLOCK; // 65

    // Number of blocks needed (last block may be padded with silence).
    size_t numBlocks = (totalSamples + (size_t)(SPB - 1)) / (size_t)SPB;
    assert(numBlocks > 0);

    size_t outputSize = sizeof(GlaHeader) + numBlocks * (size_t)BLOCK_BYTES;
    output.resize(outputSize);

    // Write GlaHeader.
    GlaHeader hdr;
    memcpy(hdr.sig, "GLAD", 4);
    hdr.version       = GLA_VERSION;
    hdr.sampleRate    = (Uint32)sampleRate;
    hdr.channels      = (Uint16)channels;
    hdr.blockSizeBytes  = (Uint16)BLOCK_BYTES;
    hdr.samplesPerBlock = (Uint32)SPB;
    hdr.totalSamples  = (Uint32)totalSamples;
    hdr.loopStart     = 0;
    hdr.loopEnd       = 0;
    memcpy(output.data(), &hdr, sizeof(hdr));

    Uint8* blockPtr = reinterpret_cast<Uint8*>(output.data()) + sizeof(GlaHeader);

    if (channels == 1) {
        // Mono: carry predictor/stepIndex across blocks for minimal discontinuity.
        int predictor = 0, stepIndex = 0;

        for (size_t b = 0; b < numBlocks; b++) {
            size_t base = b * (size_t)SPB;

            // The first sample of the block becomes the block-header predictor.
            int firstSample = (base < totalSamples)
                              ? (int)samples[base]
                              : predictor;
            predictor = firstSample;

            // Write block header: int16 predictor (LE) + uint8 stepIndex + uint8 pad.
            Uint16 upred = (Uint16)(Sint16)predictor;
            blockPtr[0] = (Uint8)(upred & 0xFF);
            blockPtr[1] = (Uint8)(upred >> 8);
            blockPtr[2] = (Uint8)stepIndex;
            blockPtr[3] = 0;

            // Encode 64 samples as 32 bytes (two nibbles per byte, low nibble first).
            for (int i = 0; i < 32; i++) {
                size_t i0 = base + 1 + (size_t)(i * 2);
                size_t i1 = i0 + 1;
                int s0 = (i0 < totalSamples) ? (int)samples[i0] : predictor;
                int s1 = (i1 < totalSamples) ? (int)samples[i1] : predictor;
                int n0 = imaEncodeNibble(s0, &predictor, &stepIndex);
                int n1 = imaEncodeNibble(s1, &predictor, &stepIndex);
                blockPtr[4 + i] = (Uint8)(n0 | (n1 << 4));
            }
            blockPtr += GLA_MONO_BLOCK_BYTES;
        }

    } else {
        // Stereo: interleaved pairs [4 bytes L][4 bytes R] after the 8-byte dual header.
        int predL = 0, stepL = 0;
        int predR = 0, stepR = 0;

        for (size_t b = 0; b < numBlocks; b++) {
            size_t base = b * (size_t)SPB; // per-channel sample offset

            int firstL = (base < totalSamples) ? (int)samples[base * 2]     : predL;
            int firstR = (base < totalSamples) ? (int)samples[base * 2 + 1] : predR;
            predL = firstL;
            predR = firstR;

            // Write dual channel headers.
            Uint16 upL = (Uint16)(Sint16)predL;
            Uint16 upR = (Uint16)(Sint16)predR;
            blockPtr[0] = (Uint8)(upL & 0xFF);
            blockPtr[1] = (Uint8)(upL >> 8);
            blockPtr[2] = (Uint8)stepL;
            blockPtr[3] = 0;
            blockPtr[4] = (Uint8)(upR & 0xFF);
            blockPtr[5] = (Uint8)(upR >> 8);
            blockPtr[6] = (Uint8)stepR;
            blockPtr[7] = 0;

            // 8 groups of [4 bytes L][4 bytes R] = 64 bytes = 8*8 = 64 samples/channel.
            for (int g = 0; g < 8; g++) {
                size_t groupBase = base + 1 + (size_t)(g * 8); // per-channel sample index
                // 4 bytes of L nibbles (8 L samples)
                for (int b2 = 0; b2 < 4; b2++) {
                    size_t i0 = groupBase + (size_t)(b2 * 2);
                    size_t i1 = i0 + 1;
                    int s0 = (i0 < totalSamples) ? (int)samples[i0 * 2]     : predL;
                    int s1 = (i1 < totalSamples) ? (int)samples[i1 * 2]     : predL;
                    int n0 = imaEncodeNibble(s0, &predL, &stepL);
                    int n1 = imaEncodeNibble(s1, &predL, &stepL);
                    blockPtr[8 + g * 8 + b2] = (Uint8)(n0 | (n1 << 4));
                }
                // 4 bytes of R nibbles (8 R samples)
                for (int b2 = 0; b2 < 4; b2++) {
                    size_t i0 = groupBase + (size_t)(b2 * 2);
                    size_t i1 = i0 + 1;
                    int s0 = (i0 < totalSamples) ? (int)samples[i0 * 2 + 1] : predR;
                    int s1 = (i1 < totalSamples) ? (int)samples[i1 * 2 + 1] : predR;
                    int n0 = imaEncodeNibble(s0, &predR, &stepR);
                    int n1 = imaEncodeNibble(s1, &predR, &stepR);
                    blockPtr[8 + g * 8 + 4 + b2] = (Uint8)(n0 | (n1 << 4));
                }
            }
            blockPtr += GLA_STEREO_BLOCK_BYTES;
        }
    }

    cout << "WAV " << filename << ": " << totalSamples << " samples/ch, "
         << channels << " ch, " << sampleRate << " Hz -> "
         << numBlocks << " IMA4 blocks, " << outputSize << " bytes" << endl;

    return true;
}

Uint32 getFileType(const string& filename, bool isAtlas = false) {
    if (isAtlas) return RESOURCE_TYPE_IMAGE_ATLAS;
    string ext = filesystem::path(filename).extension().string();
    if (ext == ".lua") return RESOURCE_TYPE_LUA;
    if (ext == ".spv") return RESOURCE_TYPE_SHADER;
    if (ext == ".png") return RESOURCE_TYPE_IMAGE;
    if (ext == ".wav") return RESOURCE_TYPE_SOUND;
    if (ext == ".loop") return RESOURCE_TYPE_MUSIC_TRACK;
    if (ext == ".svg") return RESOURCE_TYPE_VECTOR_SHAPE;
    // Add more extensions as needed
    return RESOURCE_TYPE_UNKNOWN;
}

// Round up to next multiple of 4 for block alignment
static Uint32 alignTo4(Uint32 val) {
    return (val + 3) & ~3;
}

#ifdef ENABLE_ETC
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

}

// ============================================================
// SVG → RESOURCE_TYPE_VECTOR_SHAPE tessellation
// ============================================================
// Ear-clipping triangulation for a simple polygon (no holes).
// Returns a list of triangle indices into the polygon vertex array.
static void earClipTriangulate(const vector<float>& polyX, const vector<float>& polyY,
                               vector<Uint16>& triIndices, Uint16 baseIndex)
{
    int n = (int)polyX.size();
    if (n < 3) return;

    // Build a working list of active indices
    vector<int> indices(n);
    for (int i = 0; i < n; ++i) indices[i] = i;

    // Compute signed area to determine winding.
    // The formula sum((x_j+x_i)*(y_j-y_i)) equals -2*A where A is the standard shoelace area.
    // Standard shoelace: A > 0 = CCW in Y-up math = CW on screen (Y-down).
    // So -2*A > 0 (area > 0) means CCW on screen (Y-down); negate the usual check:
    float area = 0.0f;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        area += (polyX[j] + polyX[i]) * (polyY[j] - polyY[i]);
    }
    bool ccw = (area > 0.0f);

    auto isEar = [&](int prev, int curr, int next) -> bool {
        float ax = polyX[prev], ay = polyY[prev];
        float bx = polyX[curr], by = polyY[curr];
        float cx = polyX[next], cy = polyY[next];

        // Cross product to check convexity (must match polygon winding)
        float cross = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (ccw && cross > 0.0f) return false;
        if (!ccw && cross < 0.0f) return false;

        // Check no other vertex lies inside the triangle
        for (int i = 0; i < (int)indices.size(); ++i) {
            int vi = indices[i];
            if (vi == prev || vi == curr || vi == next) continue;
            float px = polyX[vi], py = polyY[vi];
            // Barycentric point-in-triangle test
            float d1 = (px - ax) * (by - ay) - (py - ay) * (bx - ax);
            float d2 = (px - bx) * (cy - by) - (py - by) * (cx - bx);
            float d3 = (px - cx) * (ay - cy) - (py - cy) * (ax - cx);
            bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
            bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
            if (!(hasNeg && hasPos)) return false; // inside
        }
        return true;
    };

    int limit = n * n + 4; // prevent infinite loops on degenerate polygons
    while ((int)indices.size() > 2 && limit-- > 0) {
        int m = (int)indices.size();
        bool found = false;
        for (int i = 0; i < m; ++i) {
            int prev = indices[(i + m - 1) % m];
            int curr = indices[i];
            int next = indices[(i + 1) % m];
            if (isEar(prev, curr, next)) {
                triIndices.push_back(baseIndex + (Uint16)prev);
                triIndices.push_back(baseIndex + (Uint16)curr);
                triIndices.push_back(baseIndex + (Uint16)next);
                indices.erase(indices.begin() + i);
                found = true;
                break;
            }
        }
        if (!found) break; // degenerate or already fully triangulated
    }
}

// Sample a cubic Bezier at t ∈ [0,1]
static void sampleCubic(float p0x, float p0y, float p1x, float p1y,
                         float p2x, float p2y, float p3x, float p3y,
                         float t, float& outX, float& outY)
{
    float mt = 1.0f - t;
    float mt2 = mt * mt, mt3 = mt2 * mt;
    float t2 = t * t,   t3 = t2 * t;
    outX = mt3 * p0x + 3.0f * mt2 * t * p1x + 3.0f * mt * t2 * p2x + t3 * p3x;
    outY = mt3 * p0y + 3.0f * mt2 * t * p1y + 3.0f * mt * t2 * p2y + t3 * p3y;
}

// Process an .svg file into a VectorShapeHeader + VectorVertex[] + Uint16[] binary blob.
// Tessellation strategy:
//   • Each closed subpath uses ONLY the cubic-segment endpoints as the chord polygon,
//     which is then ear-clipped into fill triangles (k=l=m=0, sign=0).
//   • For CONVEX cubic segments (where the midpoint Q=(P1+P2)/2 bulges outside the chord
//     polygon), a Loop–Blinn edge triangle is added with vertices
//     (0,0,0), (0.5,0,0.5), (1,1,1) and sign=+1.  This fills the crescent area between
//     the straight chord and the true curve boundary, making convex edges perfectly crisp.
//   • For CONCAVE segments the chord slightly over-fills; those edges are left as-is.
//   • Y coordinates are negated (SVG Y-down → world Y-up) so the vertex shader's
//     gl_Position.y = -pos.y produces a right-side-up image.
//   • All positions are normalised to [−0.5, 0.5] based on the overall bounding box.
static bool processSvgToVectorShape(const string& filename, vector<char>& output)
{
    NSVGimage* img = nsvgParseFromFile(filename.c_str(), "px", 96.0f);
    if (!img) {
        cerr << "nanosvg: failed to parse " << filename << endl;
        return false;
    }

    // Pass 1: compute global bounding box for normalisation.
    float gMinX =  1e30f, gMinY =  1e30f;
    float gMaxX = -1e30f, gMaxY = -1e30f;

    for (NSVGshape* shape = img->shapes; shape; shape = shape->next) {
        for (NSVGpath* path = shape->paths; path; path = path->next) {
            if (!path->closed) continue;
            for (int i = 0; i < path->npts - 1; i += 3) {
                float* p = &path->pts[i * 2];
                for (int s = 0; s <= 16; ++s) {
                    float t = s / 16.0f;
                    float x, y;
                    sampleCubic(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], t, x, y);
                    if (x < gMinX) gMinX = x;
                    if (y < gMinY) gMinY = y;
                    if (x > gMaxX) gMaxX = x;
                    if (y > gMaxY) gMaxY = y;
                }
            }
        }
    }

    if (gMaxX <= gMinX || gMaxY <= gMinY) {
        cerr << "SVG has no renderable closed paths: " << filename << endl;
        nsvgDelete(img);
        return false;
    }

    float centerX = (gMinX + gMaxX) * 0.5f;
    float centerY = (gMinY + gMaxY) * 0.5f;
    float normScale = max(gMaxX - gMinX, gMaxY - gMinY);
    if (normScale < 1e-6f) normScale = 1.0f;

    vector<VectorVertex> vertices;
    vector<Uint16> indices;

    for (NSVGshape* shape = img->shapes; shape; shape = shape->next) {
        for (NSVGpath* path = shape->paths; path; path = path->next) {
            if (!path->closed) continue;

            // Collect cubic segments
            struct CubicSeg { float p[8]; }; // p0x,p0y, p1x,p1y, p2x,p2y, p3x,p3y
            vector<CubicSeg> segs;
            for (int i = 0; i < path->npts - 1; i += 3) {
                float* q = &path->pts[i * 2];
                CubicSeg cs;
                cs.p[0]=q[0]; cs.p[1]=q[1];
                cs.p[2]=q[2]; cs.p[3]=q[3];
                cs.p[4]=q[4]; cs.p[5]=q[5];
                cs.p[6]=q[6]; cs.p[7]=q[7];
                segs.push_back(cs);
            }
            if (segs.empty()) continue;

            // -------------------------------------------------------
            // 1. Build chord polygon (one point per segment endpoint)
            //    Y is negated here to convert from SVG Y-down to world Y-up,
            //    matching the vertex shader convention (gl_Position.y = -pos.y).
            // -------------------------------------------------------
            vector<float> polyX, polyY;
            for (const auto& cs : segs) {
                polyX.push_back( (cs.p[0] - centerX) / normScale);
                polyY.push_back(-(cs.p[1] - centerY) / normScale);
            }
            // The closing P3 of the last segment equals P0 of the first, so no extra point.

            // Compute signed polygon area to determine winding (standard shoelace = -2A; see earClip comment)
            float polyArea = 0.0f;
            int pn = (int)polyX.size();
            for (int i = 0, j = pn - 1; i < pn; j = i++) {
                polyArea += polyX[j] * polyY[i] - polyX[i] * polyY[j];
            }
            // polyArea = -2*A: negative means CCW in Y-up math (outer contour); positive means CW (hole).
            bool ccw = (polyArea < 0.0f);

            // -------------------------------------------------------
            // 2. Ear-clip the chord polygon → fill triangles
            // -------------------------------------------------------
            if ((Uint64)vertices.size() + (Uint64)polyX.size() > 65535) {
                cerr << "Warning: SVG shape has too many vertices, skipping subpath" << endl;
                continue;
            }
            Uint16 baseVtx = (Uint16)vertices.size();
            for (int i = 0; i < pn; ++i) {
                VectorVertex v;
                v.x = polyX[i]; v.y = polyY[i];
                v.k = 0.0f; v.l = 0.0f; v.m = 0.0f; v.sign = 0.0f;
                vertices.push_back(v);
            }
            earClipTriangulate(polyX, polyY, indices, baseVtx);

            // -------------------------------------------------------
            // 3. Loop–Blinn edge triangles for CONVEX segments only
            //    (fills the crescent between chord and actual curve)
            // -------------------------------------------------------
            for (const auto& cs : segs) {
                float p0x =  (cs.p[0] - centerX) / normScale;
                float p0y = -(cs.p[1] - centerY) / normScale;
                float p3x =  (cs.p[6] - centerX) / normScale;
                float p3y = -(cs.p[7] - centerY) / normScale;
                // Quadratic approximation: midpoint of the two interior control points
                float qx =  ((cs.p[2] + cs.p[4]) * 0.5f - centerX) / normScale;
                float qy = -((cs.p[3] + cs.p[5]) * 0.5f - centerY) / normScale;

                // Cross product: which side of the chord (P0→P3) does Q lie on?
                // In Y-up math coordinates (after the Y-flip above):
                //   CCW outer contour (ccw=true): Q outside chord has cross(Q-P0, P3-P0) < 0
                //   CW hole contour (ccw=false):  Q outside chord has cross > 0
                float cross = (qx - p0x) * (p3y - p0y) - (qy - p0y) * (p3x - p0x);

                // In Y-up math: CCW outer contour has Q-outside-chord with cross < 0.
                // CW hole: Q-outside-chord has cross > 0.
                bool isConvex = ccw ? (cross < 0.0f) : (cross > 0.0f);
                if (!isConvex) {
                    // Concave segment: chord already covers (slightly over-fills). Skip.
                    continue;
                }

                if ((Uint64)vertices.size() + 3 > 65535) {
                    cerr << "Warning: SVG shape has too many vertices, skipping Loop-Blinn triangle" << endl;
                    break;
                }

                Uint16 vi = (Uint16)vertices.size();
                // V0: P0 — (k,l,m) = (0, 0, 0)
                VectorVertex v0; v0.x=p0x; v0.y=p0y; v0.k=0.0f; v0.l=0.0f; v0.m=0.0f; v0.sign=1.0f;
                // V1: Q  — (k,l,m) = (0.5, 0, 0.5)
                VectorVertex v1; v1.x=qx;  v1.y=qy;  v1.k=0.5f; v1.l=0.0f; v1.m=0.5f; v1.sign=1.0f;
                // V2: P3 — (k,l,m) = (1, 1, 1)
                VectorVertex v2; v2.x=p3x; v2.y=p3y; v2.k=1.0f; v2.l=1.0f; v2.m=1.0f; v2.sign=1.0f;
                vertices.push_back(v0);
                vertices.push_back(v1);
                vertices.push_back(v2);

                // Wind the triangle so Vulkan (with Y-flipped clip space) sees the correct face
                if (ccw) {
                    indices.push_back(vi);
                    indices.push_back(vi + 2);
                    indices.push_back(vi + 1);
                } else {
                    indices.push_back(vi);
                    indices.push_back(vi + 1);
                    indices.push_back(vi + 2);
                }
            }
        }
    }

    nsvgDelete(img);

    if (vertices.empty() || indices.empty()) {
        cerr << "SVG produced no renderable geometry: " << filename << endl;
        return false;
    }

    cout << "  SVG " << filename << ": " << vertices.size() << " verts, "
         << indices.size() << " indices" << endl;

    // Build binary output: VectorShapeHeader + VectorVertex[] + Uint16[]
    VectorShapeHeader hdr{};
    hdr.numVertices = (Uint32)vertices.size();
    hdr.numIndices  = (Uint32)indices.size();
    hdr.bboxMinX = -0.5f;
    hdr.bboxMinY = -0.5f;
    hdr.bboxMaxX =  0.5f;
    hdr.bboxMaxY =  0.5f;

    output.resize(sizeof(VectorShapeHeader)
                  + vertices.size() * sizeof(VectorVertex)
                  + indices.size()  * sizeof(Uint16));
    char* dst = output.data();
    memcpy(dst, &hdr, sizeof(hdr));
    dst += sizeof(hdr);
    memcpy(dst, vertices.data(), vertices.size() * sizeof(VectorVertex));
    dst += vertices.size() * sizeof(VectorVertex);
    memcpy(dst, indices.data(), indices.size() * sizeof(Uint16));

    return true;
}

// Process a .loop JSON file into a binary MusicTrackHeader resource.
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
            } else if (ft == RESOURCE_TYPE_SOUND) {
                // .wav -> GLA (IMA ADPCM) encoded sound resource
                if (!processWavToGla(file.filename, file.data)) {
                    cerr << "Failed to encode WAV file " << file.filename << endl;
                    return 1;
                }
            } else if (ft == RESOURCE_TYPE_VECTOR_SHAPE) {
                // .svg -> VectorShapeHeader + VectorVertex[] + Uint16[]
                if (!processSvgToVectorShape(file.filename, file.data)) {
                    cerr << "Failed to tessellate SVG file " << file.filename << endl;
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