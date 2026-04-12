#include "FontManager.h"
#include "../resources/resource.h"
#include "../vulkan/VulkanRenderer.h"
#include "../debug/ConsoleBuffer.h"
#include "../core/hash.h"
#include <cassert>

// ---------------------------------------------------------------------------
// Helpers for reading little-endian values from a byte stream.
// ---------------------------------------------------------------------------

static inline Sint16 readS16(const Uint8* p) {
    return static_cast<Sint16>(static_cast<Uint16>(p[0]) | (static_cast<Uint16>(p[1]) << 8));
}
static inline Uint16 readU16(const Uint8* p) {
    return static_cast<Uint16>(p[0]) | (static_cast<Uint16>(p[1]) << 8);
}
static inline float decodeCoord(Sint16 v) {
    return static_cast<float>(v) / FONT_COORD_SCALE;
}

// ---------------------------------------------------------------------------
// Decode a single compact SDF blob into the expanded SdfShapeHeader format.
// compactData/compactSize: the compact blob bytes.
// dst: output buffer; must be at least decodedSize(compactData) bytes.
// Returns the number of bytes written, or 0 on error.
// ---------------------------------------------------------------------------
static Uint32 decodeCompactSdfBlob(const Uint8* compactData, Uint32 compactSize,
                                    char* dst) {
    assert(compactData != nullptr);
    assert(dst != nullptr);

    if (compactSize < sizeof(CompactShapeHeader)) return 0;

    const CompactShapeHeader* csh =
        reinterpret_cast<const CompactShapeHeader*>(compactData);
    Uint32 numContours   = csh->numContours;
    Uint32 totalSegments = csh->totalSegments;

    // Validate contour header array fits within compact data.
    Uint32 contourHdrsBytes = numContours * static_cast<Uint32>(sizeof(CompactContourHeader));
    if (compactSize < sizeof(CompactShapeHeader) + contourHdrsBytes) return 0;

    // Write decoded SdfShapeHeader.
    SdfShapeHeader* dsh = reinterpret_cast<SdfShapeHeader*>(dst);
    dsh->numContours   = numContours;
    dsh->bboxMinX      = decodeCoord(csh->bboxMinX);
    dsh->bboxMinY      = decodeCoord(csh->bboxMinY);
    dsh->bboxMaxX      = decodeCoord(csh->bboxMaxX);
    dsh->bboxMaxY      = decodeCoord(csh->bboxMaxY);
    dsh->totalSegments = totalSegments;
    dsh->pad[0]        = 0;
    dsh->pad[1]        = 0;
    char* dstPtr = dst + sizeof(SdfShapeHeader);

    // Decoded SdfContourHeader array (filled in below with running segmentOffset).
    SdfContourHeader* dContours =
        reinterpret_cast<SdfContourHeader*>(dstPtr);
    dstPtr += numContours * sizeof(SdfContourHeader);

    // SdfSegment array written starting at dstPtr.
    SdfSegment* dSegs = reinterpret_cast<SdfSegment*>(dstPtr);

    // Read compact contour headers.
    const CompactContourHeader* cContours =
        reinterpret_cast<const CompactContourHeader*>(
            compactData + sizeof(CompactShapeHeader));

    // Segment stream starts immediately after compact contour headers.
    const Uint8* stream = compactData + sizeof(CompactShapeHeader) + contourHdrsBytes;

    Uint32 segOut = 0;  // index into dSegs[]
    for (Uint32 ci = 0; ci < numContours; ci++) {
        Uint32 numSegs = cContours[ci].numSegments;

        // Fill decoded contour header.
        dContours[ci].numSegments   = numSegs;
        dContours[ci].winding       = 1;   // not used by shader
        dContours[ci].segmentOffset = segOut;
        dContours[ci].pad           = 0;

        // Read explicit start point p0.
        float curX = decodeCoord(readS16(stream)); stream += 2;
        float curY = decodeCoord(readS16(stream)); stream += 2;

        // Decode each segment.
        for (Uint32 si = 0; si < numSegs; si++) {
            Uint8 type = *stream++;
            assert(segOut < totalSegments);
            SdfSegment& seg = dSegs[segOut++];
            seg.p0x = curX;
            seg.p0y = curY;
            if (type == FONT_SEG_LINE) {
                float p3x = decodeCoord(readS16(stream)); stream += 2;
                float p3y = decodeCoord(readS16(stream)); stream += 2;
                // Degenerate cubic: p1 = p0, p2 = p3.
                seg.p1x = curX;  seg.p1y = curY;
                seg.p2x = p3x;   seg.p2y = p3y;
                seg.p3x = p3x;   seg.p3y = p3y;
                curX = p3x;  curY = p3y;
            } else {
                assert(type == FONT_SEG_CUBIC);
                float p1x = decodeCoord(readS16(stream)); stream += 2;
                float p1y = decodeCoord(readS16(stream)); stream += 2;
                float p2x = decodeCoord(readS16(stream)); stream += 2;
                float p2y = decodeCoord(readS16(stream)); stream += 2;
                float p3x = decodeCoord(readS16(stream)); stream += 2;
                float p3y = decodeCoord(readS16(stream)); stream += 2;
                seg.p1x = p1x;  seg.p1y = p1y;
                seg.p2x = p2x;  seg.p2y = p2y;
                seg.p3x = p3x;  seg.p3y = p3y;
                curX = p3x;  curY = p3y;
            }
        }
    }

    assert(segOut == totalSegments);
    Uint32 written = static_cast<Uint32>(
        sizeof(SdfShapeHeader)
        + numContours   * sizeof(SdfContourHeader)
        + totalSegments * sizeof(SdfSegment));
    return written;
}

// Compute the decoded byte size of a compact SDF blob (reading only the header).
static Uint32 decodedSdfBlobSize(const Uint8* compactData, Uint32 compactSize) {
    if (compactSize < sizeof(CompactShapeHeader)) return 0;
    const CompactShapeHeader* csh =
        reinterpret_cast<const CompactShapeHeader*>(compactData);
    return static_cast<Uint32>(
        sizeof(SdfShapeHeader)
        + static_cast<Uint32>(csh->numContours)   * sizeof(SdfContourHeader)
        + static_cast<Uint32>(csh->totalSegments) * sizeof(SdfSegment));
}

// Simple insertion sort for the codepoint-sorted index array.
// Sorts cpIdx[0..n-1] by glyphs[cpIdx[i]].codepoint ascending.
static void sortCpIndex(Uint32* cpIdx, Uint32 n, const FontGlyphEntry* glyphs) {
    for (Uint32 i = 1; i < n; i++) {
        Uint32 key = cpIdx[i];
        Uint32 keycp = glyphs[key].codepoint;
        Sint32 j = static_cast<Sint32>(i) - 1;
        while (j >= 0 && glyphs[cpIdx[j]].codepoint > keycp) {
            cpIdx[j + 1] = cpIdx[j];
            j--;
        }
        cpIdx[j + 1] = key;
    }
}

FontManager::FontManager(MemoryAllocator* allocator,
                         VulkanRenderer*  renderer,
                         ConsoleBuffer*   console)
    : fonts_(*allocator, "FontManager::fonts_")
    , nextHandle_(1)
    , allocator_(allocator)
    , renderer_(renderer)
    , console_(console)
{
    assert(allocator != nullptr);
    assert(renderer  != nullptr);
    assert(console   != nullptr);
}

FontManager::~FontManager() {
    clear();
}

int FontManager::loadFont(PakResource& pakResource, const char* resourcePath) {
    assert(resourcePath != nullptr);

    Uint64 resourceId = hashCString(resourcePath);
    ResourceData rd{};
    if (!pakResource.tryGetResource(resourceId, rd)) {
        console_->log(SDL_LOG_PRIORITY_ERROR,
            "FontManager: resource not found: %s", resourcePath);
        return -1;
    }

    if (rd.size < sizeof(FontBinaryHeader)) {
        console_->log(SDL_LOG_PRIORITY_ERROR,
            "FontManager: resource too small for header: %s", resourcePath);
        return -1;
    }

    const FontBinaryHeader* hdr =
        reinterpret_cast<const FontBinaryHeader*>(rd.data);

    // Validate that disk glyph/kern tables fit within the resource.
    Uint64 diskGlyphBytes = (Uint64)hdr->numGlyphs    * sizeof(FontGlyphEntryDisk);
    Uint64 diskKernBytes  = (Uint64)hdr->numKernPairs * sizeof(FontKernPairDisk);
    Uint64 minSize        = sizeof(FontBinaryHeader) + diskGlyphBytes + diskKernBytes;
    if (rd.size < minSize) {
        console_->log(SDL_LOG_PRIORITY_ERROR,
            "FontManager: resource too small for glyph/kern tables: %s", resourcePath);
        return -1;
    }

    const char* base            = rd.data;
    const Uint8* diskGlyphBase  =
        reinterpret_cast<const Uint8*>(base + sizeof(FontBinaryHeader));
    const Uint8* diskKernBase   =
        reinterpret_cast<const Uint8*>(base + sizeof(FontBinaryHeader) + diskGlyphBytes);
    const Uint8* compactSdfBase =
        reinterpret_cast<const Uint8*>(base + sizeof(FontBinaryHeader)
                                       + diskGlyphBytes + diskKernBytes);
    Uint64 compactSdfSectionSize =
        rd.size - (sizeof(FontBinaryHeader) + diskGlyphBytes + diskKernBytes);

    // --- Decode FontGlyphEntryDisk[] -> FontGlyphEntry[] ---
    FontGlyphEntry* glyphs = nullptr;
    if (hdr->numGlyphs > 0) {
        glyphs = static_cast<FontGlyphEntry*>(
            allocator_->allocate(sizeof(FontGlyphEntry) * hdr->numGlyphs,
                                 "FontManager::glyphs"));
        assert(glyphs != nullptr);
        for (Uint32 i = 0; i < hdr->numGlyphs; i++) {
            const Uint8* diskPtr =
                diskGlyphBase + i * static_cast<Uint32>(sizeof(FontGlyphEntryDisk));
            const FontGlyphEntryDisk* dg =
                reinterpret_cast<const FontGlyphEntryDisk*>(diskPtr);
            glyphs[i].codepoint    = dg->codepoint;
            glyphs[i].glyphIndex   = i;  // array position == font-internal glyph index
            glyphs[i].advanceWidth = static_cast<Sint32>(dg->advanceWidth);
            glyphs[i].leftBearing  = static_cast<Sint32>(dg->leftBearing);
            glyphs[i].sdfOffset    = dg->sdfOffset;
            glyphs[i].sdfSize      = dg->sdfSize;
        }
    }

    // --- Decode FontKernPairDisk[] -> FontKernPair[] ---
    FontKernPair* kernPairs = nullptr;
    if (hdr->numKernPairs > 0) {
        kernPairs = static_cast<FontKernPair*>(
            allocator_->allocate(sizeof(FontKernPair) * hdr->numKernPairs,
                                 "FontManager::kernPairs"));
        assert(kernPairs != nullptr);
        for (Uint32 i = 0; i < hdr->numKernPairs; i++) {
            const Uint8* diskPtr =
                diskKernBase + i * static_cast<Uint32>(sizeof(FontKernPairDisk));
            const FontKernPairDisk* dk =
                reinterpret_cast<const FontKernPairDisk*>(diskPtr);
            kernPairs[i].leftGlyphIndex  = static_cast<Uint32>(readU16(diskPtr));
            kernPairs[i].rightGlyphIndex = static_cast<Uint32>(readU16(diskPtr + 2));
            kernPairs[i].kernValue       = dk->kernValue;
        }
    }

    // --- Compute total decoded SDF buffer size ---
    Uint64 totalDecodedSdfBytes = 0;
    for (Uint32 i = 0; i < hdr->numGlyphs; i++) {
        if (glyphs[i].sdfSize == 0) continue;
        Uint64 blobStart = glyphs[i].sdfOffset;
        if (blobStart + glyphs[i].sdfSize > compactSdfSectionSize) {
            console_->log(SDL_LOG_PRIORITY_ERROR,
                "FontManager: compact SDF blob for glyph %u out of bounds in %s",
                i, resourcePath);
            glyphs[i].sdfSize = 0;
            continue;
        }
        Uint32 decoded = decodedSdfBlobSize(
            compactSdfBase + blobStart,
            glyphs[i].sdfSize);
        totalDecodedSdfBytes += decoded;
    }

    // --- Allocate and fill decoded SDF buffer ---
    char* decodedSdfBuf = nullptr;
    if (totalDecodedSdfBytes > 0) {
        decodedSdfBuf = static_cast<char*>(
            allocator_->allocate(totalDecodedSdfBytes, "FontManager::decodedSdf"));
        assert(decodedSdfBuf != nullptr);
    }

    // Decode each compact SDF blob and update glyph sdfOffset to decoded position.
    Uint32 decodedOffset = 0;
    for (Uint32 i = 0; i < hdr->numGlyphs; i++) {
        if (glyphs[i].sdfSize == 0) continue;
        const Uint8* compact = compactSdfBase + glyphs[i].sdfOffset;
        Uint32 written = decodeCompactSdfBlob(compact, glyphs[i].sdfSize,
                                               decodedSdfBuf + decodedOffset);
        assert(written > 0);
        console_->log(SDL_LOG_PRIORITY_VERBOSE,
            "FontManager: glyph %u decoded %u -> %u bytes",
            i, glyphs[i].sdfSize, written);
        // Repoint sdfOffset to position in decoded buffer; update sdfSize to decoded size.
        glyphs[i].sdfOffset = decodedOffset;
        glyphs[i].sdfSize   = written;
        decodedOffset += written;
    }
    assert(decodedOffset == totalDecodedSdfBytes);

    // --- Build codepoint-sorted index for O(log N) lookupGlyph ---
    Uint32* cpSortedIndex = nullptr;
    if (hdr->numGlyphs > 0) {
        cpSortedIndex = static_cast<Uint32*>(
            allocator_->allocate(sizeof(Uint32) * hdr->numGlyphs,
                                 "FontManager::cpSortedIndex"));
        assert(cpSortedIndex != nullptr);
        for (Uint32 i = 0; i < hdr->numGlyphs; i++) cpSortedIndex[i] = i;
        sortCpIndex(cpSortedIndex, hdr->numGlyphs, glyphs);
    }

    // --- Allocate per-glyph shape ID array ---
    Uint64* glyphShapeIds = nullptr;
    if (hdr->numGlyphs > 0) {
        glyphShapeIds = static_cast<Uint64*>(
            allocator_->allocate(sizeof(Uint64) * hdr->numGlyphs,
                                 "FontManager::glyphShapeIds"));
        assert(glyphShapeIds != nullptr);
        for (Uint32 i = 0; i < hdr->numGlyphs; i++) glyphShapeIds[i] = 0;
    }

    // --- Upload each glyph's decoded SDF blob to the GPU ---
    // Shape ID = fontResourceId XOR'd with a mixing constant * (glyphIndex + 1)
    // to give a distinct per-glyph ID that is deterministic and collision-free.
    for (Uint32 i = 0; i < hdr->numGlyphs; i++) {
        if (glyphs[i].sdfSize == 0) {
            glyphShapeIds[i] = 0;
            continue;
        }

        Uint64 shapeId = resourceId ^ ((Uint64)(i + 1) * 2654435761ULL);
        glyphShapeIds[i] = shapeId;

        ResourceData sdfRd{};
        sdfRd.data = decodedSdfBuf + glyphs[i].sdfOffset;
        sdfRd.size = glyphs[i].sdfSize;
        sdfRd.type = RESOURCE_TYPE_VECTOR_SHAPE;

        renderer_->loadVectorShape(shapeId, sdfRd);
    }

    // --- Assemble LoadedFont ---
    LoadedFont* font =
        static_cast<LoadedFont*>(allocator_->allocate(sizeof(LoadedFont), "FontManager::loadFont"));
    assert(font != nullptr);
    new (font) LoadedFont();

    font->header        = *hdr;
    font->glyphs        = glyphs;
    font->kernPairs     = kernPairs;
    font->sdfSection    = decodedSdfBuf;
    font->cpSortedIndex = cpSortedIndex;
    font->glyphShapeIds = glyphShapeIds;
    font->numShapeIds   = hdr->numGlyphs;

    int handle = nextHandle_++;
    fonts_.insert(handle, font);

    console_->log(SDL_LOG_PRIORITY_VERBOSE,
        "FontManager: loaded %s handle=%d glyphs=%u kern=%u",
        resourcePath, handle, hdr->numGlyphs, hdr->numKernPairs);

    return handle;
}

void FontManager::unloadFont(int handle) {
    LoadedFont** ptr = fonts_.find(handle);
    if (ptr == nullptr) return;
    LoadedFont* font = *ptr;
    assert(font != nullptr);
    destroyFont(font);
    fonts_.remove(handle);
}

const FontGlyphEntry* FontManager::lookupGlyph(int handle, Uint32 codepoint) const {
    const LoadedFont* const* ptr = fonts_.find(handle);
    if (ptr == nullptr) return nullptr;
    const LoadedFont* font = *ptr;

    // Binary search using codepoint-sorted index.
    Uint32 lo = 0, hi = font->header.numGlyphs;
    while (lo < hi) {
        Uint32 mid = lo + (hi - lo) / 2;
        Uint32 idx = font->cpSortedIndex[mid];
        Uint32 cp  = font->glyphs[idx].codepoint;
        if (cp < codepoint) {
            lo = mid + 1;
        } else if (cp > codepoint) {
            hi = mid;
        } else {
            return &font->glyphs[idx];
        }
    }
    return nullptr;
}

const FontGlyphEntry* FontManager::lookupGlyphByIndex(int handle, Uint32 glyphIndex) const {
    const LoadedFont* const* ptr = fonts_.find(handle);
    if (ptr == nullptr) return nullptr;
    const LoadedFont* font = *ptr;

    // Entries are stored in glyph-index order: direct O(1) access.
    if (glyphIndex >= font->header.numGlyphs) return nullptr;
    return &font->glyphs[glyphIndex];
}

Sint32 FontManager::getKern(int handle,
                             Uint32 leftGlyphIndex,
                             Uint32 rightGlyphIndex) const {
    const LoadedFont* const* ptr = fonts_.find(handle);
    if (ptr == nullptr) return 0;
    const LoadedFont* font = *ptr;

    if (font->header.numKernPairs == 0) return 0;

    // Binary search on sorted (left, right) pairs.
    Uint32 lo = 0, hi = font->header.numKernPairs;
    while (lo < hi) {
        Uint32 mid = lo + (hi - lo) / 2;
        const FontKernPair& kp = font->kernPairs[mid];
        if (kp.leftGlyphIndex < leftGlyphIndex) {
            lo = mid + 1;
        } else if (kp.leftGlyphIndex > leftGlyphIndex) {
            hi = mid;
        } else {
            // Left matches; search on rightGlyphIndex.
            if (kp.rightGlyphIndex < rightGlyphIndex) {
                lo = mid + 1;
            } else if (kp.rightGlyphIndex > rightGlyphIndex) {
                hi = mid;
            } else {
                return kp.kernValue;
            }
        }
    }
    return 0;
}

Sint32 FontManager::getUnitsPerEM(int handle) const {
    const LoadedFont* const* ptr = fonts_.find(handle);
    return ptr ? (*ptr)->header.unitsPerEM : 1;
}

Sint32 FontManager::getAscender(int handle) const {
    const LoadedFont* const* ptr = fonts_.find(handle);
    return ptr ? (*ptr)->header.ascender : 0;
}

Sint32 FontManager::getDescender(int handle) const {
    const LoadedFont* const* ptr = fonts_.find(handle);
    return ptr ? (*ptr)->header.descender : 0;
}

Sint32 FontManager::getLineGap(int handle) const {
    const LoadedFont* const* ptr = fonts_.find(handle);
    return ptr ? (*ptr)->header.lineGap : 0;
}

Uint64 FontManager::getGlyphShapeId(int handle, Uint32 glyphIndex) const {
    const LoadedFont* const* ptr = fonts_.find(handle);
    if (ptr == nullptr) return 0;
    const LoadedFont* font = *ptr;

    // Entries are in glyph-index order: direct O(1) access.
    if (glyphIndex >= font->numShapeIds) return 0;
    return font->glyphShapeIds[glyphIndex];
}

const char* FontManager::getGlyphSdfData(int handle, Uint32 glyphIndex, Uint32* outSize) const {
    assert(outSize != nullptr);
    *outSize = 0;

    const LoadedFont* const* ptr = fonts_.find(handle);
    if (ptr == nullptr) return nullptr;
    const LoadedFont* font = *ptr;

    // Entries are in glyph-index order: direct O(1) access.
    if (glyphIndex >= font->header.numGlyphs) return nullptr;
    const FontGlyphEntry& ge = font->glyphs[glyphIndex];
    if (ge.sdfSize == 0) return nullptr;
    *outSize = ge.sdfSize;
    return font->sdfSection + ge.sdfOffset;
}

bool FontManager::isValid(int handle) const {
    return fonts_.find(handle) != nullptr;
}

void FontManager::clear() {
    for (auto it = fonts_.begin(); it != fonts_.end(); ++it) {
        LoadedFont* font = it.value();
        assert(font != nullptr);
        destroyFont(font);
    }
    fonts_.clear();
}

void FontManager::destroyFont(LoadedFont* font) {
    assert(font != nullptr);
    if (font->glyphShapeIds) {
        allocator_->free(font->glyphShapeIds);
        font->glyphShapeIds = nullptr;
    }
    if (font->cpSortedIndex) {
        allocator_->free(font->cpSortedIndex);
        font->cpSortedIndex = nullptr;
    }
    if (font->sdfSection) {
        allocator_->free(font->sdfSection);
        font->sdfSection = nullptr;
    }
    if (font->kernPairs) {
        allocator_->free(font->kernPairs);
        font->kernPairs = nullptr;
    }
    if (font->glyphs) {
        allocator_->free(font->glyphs);
        font->glyphs = nullptr;
    }
    font->~LoadedFont();
    allocator_->free(font);
}
