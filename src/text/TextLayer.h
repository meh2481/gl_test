#pragma once

#include <SDL3/SDL_stdinc.h>
#include <lua.hpp>
#include "TextLayout.h"
#include "../core/Vector.h"
#include "../memory/MemoryAllocator.h"

class FontManager;
class VulkanRenderer;
class ConsoleBuffer;

// Effect types for per-character markup spans.
enum MarkupEffect {
    MARKUP_EFFECT_NONE    = 0,
    MARKUP_EFFECT_COLOR   = 1,  // params: r,g,b,a
    MARKUP_EFFECT_WAVE    = 2,  // params: amp, freq, 0, 0
    MARKUP_EFFECT_SHAKE   = 3,  // params: mag, 0, 0, 0
    MARKUP_EFFECT_RAINBOW = 4,  // params: speed, 0, 0, 0
    MARKUP_EFFECT_SCALE   = 5,  // params: scale, 0, 0, 0
    MARKUP_EFFECT_FONT    = 6,  // fontHandle stored in fontHandle field
    MARKUP_EFFECT_UNDERLINE = 7, // underline glyph quads for [underline] spans
};

struct MarkupSpan {
    int          startChar;  // inclusive, in the plain text
    int          endChar;    // exclusive, in the plain text
    MarkupEffect effect;
    float        params[4];
    int          fontHandle; // for MARKUP_EFFECT_FONT; 0 otherwise
};

// TextLayer owns a laid-out string and one batched GPU draw call (M8 text pipeline).
// It supports typewriter reveal, per-character effects, and markup tags.
//
// Lifecycle:
//   createTextLayer → setString (triggers rebuild) → update each frame
//   destroyTextLayer → destroyGlyphLayers + free
//
// Rebuild must be called with the active sceneId so the GPU resources are
// associated with the correct scene (for automatic cleanup).
class TextLayer {
public:
    TextLayer(MemoryAllocator* allocator,
              FontManager*     fontManager,
              VulkanRenderer*  renderer,
              ConsoleBuffer*   console);
    ~TextLayer();

    // --- Configuration ---
    void setFont(int fontHandle);
    void setPosition(float x, float y);
    void setSize(float pointSize);
    void setColor(float r, float g, float b, float a);
    void setWrapWidth(float width);
    void setLineSpacing(float mult);
    void setAlignment(int align);

    // Set the font family for [font=bold/italic/bolditalic] markup resolution.
    // Pass -1 for any variant that is not available.
    void setFontFamily(int boldHandle, int italicHandle, int boldItalicHandle);

    // M8: Drop shadow.  Offset is in the same world-space units as position/scale.
    // Call with a=0 (or clearShadow) to disable.
    void setShadow(float dx, float dy, float r, float g, float b, float a);
    void clearShadow();

    // Set the string (parses markup, runs layout, uploads GPU resources).
    // Must be called with the active sceneId.
    void setString(const char* text, Uint64 sceneId);

    // --- Reveal (typewriter) animation ---
    void setRevealSpeed(float charsPerSecond);  // 0 = instant reveal
    void setRevealCount(int n);

    // Lua callbacks (LUA_NOREF = none)
    void setOnRevealComplete(lua_State* L, int funcRef);
    void setOnCharRevealed(lua_State* L, int funcRef);

    // --- Per-frame update (advance reveal, apply effects, upload vertex data) ---
    void update(float dt, Uint64 sceneId);

    // Destroy all owned GPU resources.  Call before scene cleanup for explicit
    // teardown; scene cleanup also wipes them via renderer_.clearTextLayersForScene.
    void destroyGlyphLayers();

    // Rebuild GPU resources from current params.  Called internally by setString.
    void rebuild(Uint64 sceneId);

    // Parsed markup spans (built by setString).
    int               getSpanCount() const { return (int)spans_.size(); }
    const MarkupSpan& getSpan(int i) const { return spans_[i]; }

    int getTotalChars() const { return totalChars_; }
    int getRevealCount() const { return revealCount_; }

    // Returns true if all glyphs with index < upToCharIndex have finished their
    // fade-in animation (revealTimer >= FADE_IN_TIME, or not yet revealed but
    // below the threshold which shouldn't happen in normal flow).
    bool isRevealAnimComplete(int upToCharIndex) const;

    // Like update(), but advances only the fade-in timers and effects for
    // already-revealed glyphs — does NOT advance the reveal accumulator.
    // Use this when you want characters to finish fading in without revealing
    // additional characters (e.g. while waiting before starting a pause).
    void updateFadesAndEffects(float dt, Uint64 sceneId);

private:
    // Per-glyph runtime state (M8: no per-glyph VectorLayer IDs)
    struct GlyphLayerInfo {
        float baseX, baseY;   // layout position (before effects)
        float baseR, baseG, baseB, baseA;  // base colour
        float revealTimer;    // 0..FADE_IN_TIME during fade-in, <0 = not yet revealed
        int   charIndex;      // position in the plain text (for markup range checks)
        float advanceX;       // character advance in world units (for progressive underline)
        bool  revealed;
        bool  hasOutline;     // false for space, newline, or glyphs with no SDF blob
        bool  isUnderlineRun; // true for synthetic [underline] run geometry
        int   underlineEndChar; // exclusive char index for underline run
        float underlineFullMaxX; // local-space full run max X for underline geometry

        // M8 text-pipeline fields (valid only when hasOutline)
        int   sdfGlyphIdx;    // index in GlyphDesc SSBO / vertex buffer (-1 if !hasOutline)
        float bboxMinX, bboxMinY, bboxMaxX, bboxMaxY;  // shape-local bbox from SdfShapeHeader
        float glyphScale;     // = layerScale * unitsPerEM (world units per shape-local unit)
    };

    // Parse markup from raw text; fills plainText_ and spans_.
    void parseMarkup(const char* raw);

    void applyEffects(float dt);
    void updateReveal(float dt, Uint64 sceneId);
    void updateUnderlineRunRevealGeometry();

    // Helpers that write into / update cpuVertices_.
    // vertIdx is the index of the first of 6 vertices for this glyph (in units of 11 floats).
    static void writeGlyphQuad(float* buf, int vertStartIdx, int glyphDescIdx,
                               float worldOriginX, float worldOriginY,
                               float bboxMinX, float bboxMinY,
                               float bboxMaxX, float bboxMaxY,
                               float glyphScale,
                               float r, float g, float b, float a);
    static void updateGlyphQuadColor(float* buf, int vertStartIdx,
                                     float r, float g, float b, float a);
    static void updateGlyphQuadOffset(float* buf, int vertStartIdx,
                                      float xOff, float yOff);

    MemoryAllocator* allocator_;
    FontManager*     fontManager_;
    VulkanRenderer*  renderer_;
    ConsoleBuffer*   console_;

    int    fontHandle_;
    float  posX_, posY_;
    float  pointSize_;
    float  colorR_, colorG_, colorB_, colorA_;
    float  wrapWidth_;
    float  lineSpacingMult_;
    int    alignment_;

    // Font family for [font=bold/italic/bolditalic] markup (M6)
    int    fontFamilyBold_;
    int    fontFamilyItalic_;
    int    fontFamilyBoldItalic_;

    // M8: Drop shadow
    bool   shadowEnabled_;
    float  shadowDX_, shadowDY_;
    float  shadowR_, shadowG_, shadowB_, shadowA_;

    // Owned copy of the plain (markup-stripped) text.
    char*  plainText_;
    Uint64 sceneId_;   // scene this layer belongs to

    float  revealSpeed_;    // chars/s (0 = instant)
    float  revealAccum_;    // fractional accumulator
    int    revealCount_;    // how many chars are visible
    int    totalChars_;     // total laid-out chars
    bool   revealComplete_; // has the reveal finished?

    float  time_;  // accumulated time for effects

    Vector<GlyphLayerInfo> glyphLayers_;
    Vector<MarkupSpan>     spans_;

    // M8 GPU resources
    int              textLayerGpuId_;   // -1 if no GPU resources exist
    int              numSdfGlyphs_;     // SDF glyph count (outline glyphs only)
    int              totalSdfVertices_; // numSdfGlyphs_ * 6 * (shadow ? 2 : 1)
    int              mainVertOffset_;   // start index (in glyphs) of main quads in vertex buf
    Vector<float>    cpuVertices_;      // CPU copy of the vertex buffer (11 floats/vertex)
    bool             verticesDirty_;    // true when cpuVertices_ has unsent changes

    lua_State* lua_;
    int  onRevealCompleteRef_;
    int  onCharRevealedRef_;

    static const float FADE_IN_TIME;  // seconds for per-char fade-in
};
