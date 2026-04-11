#include "FontManager.h"
#include "../resources/resource.h"
#include "../vulkan/VulkanRenderer.h"
#include "../debug/ConsoleBuffer.h"
#include "../core/hash.h"
#include <cassert>

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

    if (hdr->magic != FONT_BINARY_MAGIC) {
        console_->log(SDL_LOG_PRIORITY_ERROR,
            "FontManager: bad magic in %s (got 0x%08X, expected 0x%08X)",
            resourcePath, hdr->magic, FONT_BINARY_MAGIC);
        return -1;
    }

    Uint64 glyphBytes = (Uint64)hdr->numGlyphs    * sizeof(FontGlyphEntry);
    Uint64 kernBytes  = (Uint64)hdr->numKernPairs * sizeof(FontKernPair);
    Uint64 minSize    = sizeof(FontBinaryHeader) + glyphBytes + kernBytes;
    if (rd.size < minSize) {
        console_->log(SDL_LOG_PRIORITY_ERROR,
            "FontManager: resource too small for glyph/kern tables: %s", resourcePath);
        return -1;
    }

    LoadedFont* font =
        static_cast<LoadedFont*>(allocator_->allocate(sizeof(LoadedFont), "FontManager::loadFont"));
    assert(font != nullptr);
    new (font) LoadedFont();

    font->header       = *hdr;
    font->resourceData = rd.data;

    const char* base     = rd.data;
    font->glyphs         = reinterpret_cast<const FontGlyphEntry*>(base + sizeof(FontBinaryHeader));
    font->kernPairs      = reinterpret_cast<const FontKernPair*>(base + sizeof(FontBinaryHeader) + glyphBytes);
    font->sdfSection     = base + sizeof(FontBinaryHeader) + glyphBytes + kernBytes;

    // Allocate per-glyph shape ID array
    font->numShapeIds    = hdr->numGlyphs;
    if (hdr->numGlyphs > 0) {
        font->glyphShapeIds =
            static_cast<Uint64*>(allocator_->allocate(
                sizeof(Uint64) * hdr->numGlyphs, "FontManager::glyphShapeIds"));
        assert(font->glyphShapeIds != nullptr);
        for (Uint32 i = 0; i < hdr->numGlyphs; i++) {
            font->glyphShapeIds[i] = 0;
        }
    } else {
        font->glyphShapeIds = nullptr;
    }

    // Upload each glyph's SDF blob to the GPU.
    // Shape ID = fontResourceId XOR'd with a mixing constant * (glyphIndex + 1)
    // to give a distinct per-glyph ID that is deterministic and collision-free
    // (assuming the font is not loaded under two different path names at once).
    for (Uint32 i = 0; i < hdr->numGlyphs; i++) {
        const FontGlyphEntry& ge = font->glyphs[i];
        if (ge.sdfSize == 0) {
            // No outline (space, newline, etc.)
            font->glyphShapeIds[i] = 0;
            continue;
        }

        Uint64 shapeId = resourceId ^ ((Uint64)(ge.glyphIndex + 1) * 2654435761ULL);
        font->glyphShapeIds[i] = shapeId;

        // Validate the SDF blob fits within the resource.
        Uint64 blobStart  = (Uint64)(font->sdfSection - rd.data) + ge.sdfOffset;
        if (blobStart + ge.sdfSize > rd.size) {
            console_->log(SDL_LOG_PRIORITY_ERROR,
                "FontManager: SDF blob for glyph %u out of bounds in %s",
                ge.glyphIndex, resourcePath);
            font->glyphShapeIds[i] = 0;
            continue;
        }

        ResourceData sdfRd{};
        sdfRd.data = const_cast<char*>(font->sdfSection + ge.sdfOffset);
        sdfRd.size = ge.sdfSize;
        sdfRd.type = RESOURCE_TYPE_VECTOR_SHAPE;

        renderer_->loadVectorShape(shapeId, sdfRd);
    }

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

    // Binary search on sorted codepoint array.
    Uint32 lo = 0, hi = font->header.numGlyphs;
    while (lo < hi) {
        Uint32 mid = lo + (hi - lo) / 2;
        if (font->glyphs[mid].codepoint < codepoint) {
            lo = mid + 1;
        } else if (font->glyphs[mid].codepoint > codepoint) {
            hi = mid;
        } else {
            return &font->glyphs[mid];
        }
    }
    return nullptr;
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

    // Find the entry for this glyphIndex (linear search — could be indexed map
    // if needed, but glyph counts are small).
    for (Uint32 i = 0; i < font->header.numGlyphs; i++) {
        if (font->glyphs[i].glyphIndex == glyphIndex) {
            return font->glyphShapeIds[i];
        }
    }
    return 0;
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
    font->~LoadedFont();
    allocator_->free(font);
}
