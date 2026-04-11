#pragma once

#include <SDL3/SDL_stdinc.h>
#include "../core/Vector.h"
#include "../memory/MemoryAllocator.h"

class FontManager;

// Alignment constants for textLayerSetAlignment
enum TextAlignment {
    TEXT_ALIGN_LEFT   = 0,
    TEXT_ALIGN_CENTER = 1,
    TEXT_ALIGN_RIGHT  = 2,
};

// One placed glyph produced by the layout engine.
struct GlyphInstance {
    Uint32 codepoint;
    Uint32 glyphIndex;
    int    fontHandle;
    float  x, y;       // world-space origin of this glyph (cursor origin, Y = baseline)
    float  scale;      // pointSize / unitsPerEM (world units per normalised unit)
    int    charIndex;  // index of this character in the plain (markup-stripped) string
    bool   hasOutline; // false for space, newline, tab, or glyphs with no SDF
};

// Parameters for a single layout pass.
struct TextLayoutParams {
    int   fontHandle;
    float originX, originY; // top-left of the first line (baseline is below by ascender*scale)
    float pointSize;        // desired font height in world units (= 1 em)
    float wrapWidth;        // 0 = no wrapping
    float lineSpacingMult;  // line spacing multiplier (1.0 = default metrics)
    int   alignment;        // TextAlignment enum value
};

// Lays out a UTF-8 string into a flat array of GlyphInstances.
class TextLayout {
public:
    TextLayout(MemoryAllocator* allocator, FontManager* fontManager);
    ~TextLayout() = default;

    // Run a layout pass.  Results are available via getGlyph() / getGlyphCount().
    void layout(const char* text, const TextLayoutParams& params);

    int               getGlyphCount() const { return (int)glyphs_.size(); }
    const GlyphInstance& getGlyph(int i) const { return glyphs_[i]; }

    // Byte length of the plain text used for the last layout.
    int getTextByteLength() const { return textByteLen_; }

private:
    // Decode one UTF-8 codepoint from *p; advance *p past it.
    // Returns the codepoint, or 0xFFFD on error.
    static Uint32 decodeUtf8(const char** p);

    Vector<GlyphInstance> glyphs_;
    FontManager*          fontManager_;
    MemoryAllocator*      allocator_;
    int                   textByteLen_;
};
