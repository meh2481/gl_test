#pragma once

#include <SDL3/SDL_stdinc.h>
#include "../core/ResourceTypes.h"
#include "../core/HashTable.h"
#include "../memory/MemoryAllocator.h"

class VulkanRenderer;
class PakResource;
class ConsoleBuffer;
struct ResourceData;

// FontManager loads binary .font resources from the pak, registers per-glyph
// SDF shapes with VulkanRenderer, and provides fast glyph / kerning lookup.
//
// All glyph outline data is normalised by the font's unitsPerEM:
//   normalised_coord = design_unit_value / unitsPerEM
// So setting modelScale = pointSize (world units) in createVectorLayer gives
// text at the requested point size.
class FontManager {
public:
    FontManager(MemoryAllocator* allocator,
                VulkanRenderer*  renderer,
                ConsoleBuffer*   console);
    ~FontManager();

    // Load a font from the pak by resource path (e.g. "res/fonts/aileron/Aileron-Regular.font").
    // Returns a font handle (>= 0) on success, -1 on failure.
    int loadFont(PakResource& pakResource, const char* resourcePath);

    // Unload a previously loaded font and free its GPU resources.
    void unloadFont(int handle);

    // Glyph lookup by Unicode codepoint.  Returns nullptr if not found.
    const FontGlyphEntry* lookupGlyph(int handle, Uint32 codepoint) const;

    // Glyph lookup by glyph index.  Returns nullptr if not found.
    const FontGlyphEntry* lookupGlyphByIndex(int handle, Uint32 glyphIndex) const;

    // Kerning lookup.  Returns kerning value in design units (0 if no pair).
    Sint32 getKern(int handle, Uint32 leftGlyphIndex, Uint32 rightGlyphIndex) const;

    // Font metrics (all in design units).
    Sint32 getUnitsPerEM (int handle) const;
    Sint32 getAscender   (int handle) const;
    Sint32 getDescender  (int handle) const;
    Sint32 getLineGap    (int handle) const;

    // Retrieve the Vulkan shape ID assigned to a glyph (pass to createVectorLayer).
    // Returns 0 if the glyph has no outline or the handle is invalid.
    Uint64 getGlyphShapeId(int handle, Uint32 glyphIndex) const;

    // Return a pointer to the raw SDF blob (SdfShapeHeader + contours + segments)
    // for the glyph identified by glyphIndex.  *outSize is set to the blob byte length.
    // Returns nullptr if the glyph has no outline, the handle is invalid, or the
    // glyph is not found.  The pointer is valid as long as the font remains loaded.
    const char* getGlyphSdfData(int handle, Uint32 glyphIndex, Uint32* outSize) const;

    bool isValid(int handle) const;

    // Destroy all loaded fonts and their GPU resources.
    void clear();

private:
    struct LoadedFont {
        FontBinaryHeader header;
        // Pointers into the borrowed resource data (PakResource keeps it alive).
        const FontGlyphEntry* glyphs;
        const FontKernPair*   kernPairs;
        const char*           sdfSection;   // start of the SDF blobs region
        const char*           resourceData; // borrowed pointer

        // Per-glyph shape IDs (index matches glyphs[] index, 0 = no outline).
        Uint64*  glyphShapeIds;
        Uint32   numShapeIds;   // == header.numGlyphs
    };

    HashTable<int, LoadedFont*> fonts_;
    int nextHandle_;

    MemoryAllocator* allocator_;
    VulkanRenderer*  renderer_;
    ConsoleBuffer*   console_;

    void destroyFont(LoadedFont* font);
};
