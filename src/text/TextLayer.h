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
};

struct MarkupSpan {
    int          startChar;  // inclusive, in the plain text
    int          endChar;    // exclusive, in the plain text
    MarkupEffect effect;
    float        params[4];
};

// TextLayer owns a laid-out string and one VectorLayer per glyph.
// It supports typewriter reveal, per-character effects, and markup tags.
//
// Lifecycle:
//   createTextLayer → setString (triggers rebuild) → update each frame
//   destroyTextLayer → destroyGlyphLayers + free
//
// Rebuild must be called with the active sceneId so newly created VectorLayers
// are associated with the correct scene (for automatic cleanup).
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

    // Set the string (parses markup, runs layout, rebuilds VectorLayers).
    // Must be called with the active sceneId.
    void setString(const char* text, Uint64 sceneId);

    // --- Reveal (typewriter) animation ---
    void setRevealSpeed(float charsPerSecond);  // 0 = instant reveal
    void setRevealCount(int n);

    // Lua callbacks (LUA_NOREF = none)
    void setOnRevealComplete(lua_State* L, int funcRef);
    void setOnCharRevealed(lua_State* L, int funcRef);

    // --- Per-frame update (advance reveal, apply effects) ---
    void update(float dt, Uint64 sceneId);

    // Destroy all owned VectorLayers.  Call before scene cleanup if you want
    // explicit teardown; the scene cleanup will also wipe them via
    // renderer_.clearVectorLayersForScene.
    void destroyGlyphLayers();

    // Rebuild VectorLayers from current params.  Called internally by setString.
    void rebuild(Uint64 sceneId);

    // Parsed markup spans (built by setString).
    int           getSpanCount() const { return (int)spans_.size(); }
    const MarkupSpan& getSpan(int i) const { return spans_[i]; }

    int getTotalChars() const { return totalChars_; }
    int getRevealCount() const { return revealCount_; }

private:
    // Per-glyph runtime state
    struct GlyphLayerInfo {
        int   vectorLayerId;  // -1 if glyph has no outline (space, etc.)
        float baseX, baseY;   // layout position (before effects)
        float baseR, baseG, baseB, baseA;  // base colour
        float revealTimer;    // 0..FADE_IN_TIME during fade-in, <0 = not yet revealed
        int   charIndex;      // position in the plain text (for markup range checks)
        bool  revealed;
    };

    // Parse markup from raw text; fills plains_ and spans_.
    void parseMarkup(const char* raw);

    void applyEffects(float dt);
    void updateReveal(float dt, Uint64 sceneId);

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

    // Owned copy of the plain (markup-stripped) text.
    char*  plainText_;
    Uint64 sceneId_;   // scene this layer belongs to (for createVectorLayer)

    float  revealSpeed_;    // chars/s (0 = instant)
    float  revealAccum_;    // fractional accumulator
    int    revealCount_;    // how many chars are visible
    int    totalChars_;     // total laid-out chars
    bool   revealComplete_; // has the reveal finished?

    float  time_;  // accumulated time for effects

    Vector<GlyphLayerInfo> glyphLayers_;
    Vector<MarkupSpan>     spans_;

    lua_State* lua_;
    int  onRevealCompleteRef_;
    int  onCharRevealedRef_;

    static const float FADE_IN_TIME;  // seconds for per-char fade-in (0 = instant)
};
