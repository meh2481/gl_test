#include "TextLayer.h"
#include "FontManager.h"
#include "../vulkan/VulkanRenderer.h"
#include "../debug/ConsoleBuffer.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <cstring>

// Per-character fade-in duration in seconds.
const float TextLayer::FADE_IN_TIME = 0.08f;

TextLayer::TextLayer(MemoryAllocator* allocator,
                     FontManager*     fontManager,
                     VulkanRenderer*  renderer,
                     ConsoleBuffer*   console)
    : allocator_(allocator)
    , fontManager_(fontManager)
    , renderer_(renderer)
    , console_(console)
    , fontHandle_(-1)
    , posX_(0.0f), posY_(0.0f)
    , pointSize_(24.0f)
    , colorR_(1.0f), colorG_(1.0f), colorB_(1.0f), colorA_(1.0f)
    , wrapWidth_(0.0f)
    , lineSpacingMult_(1.0f)
    , alignment_(TEXT_ALIGN_LEFT)
    , fontFamilyBold_(-1)
    , fontFamilyItalic_(-1)
    , fontFamilyBoldItalic_(-1)
    , plainText_(nullptr)
    , sceneId_(0)
    , revealSpeed_(0.0f)
    , revealAccum_(0.0f)
    , revealCount_(0)
    , totalChars_(0)
    , revealComplete_(true)
    , time_(0.0f)
    , glyphLayers_(*allocator, "TextLayer::glyphLayers_")
    , spans_(*allocator, "TextLayer::spans_")
    , lua_(nullptr)
    , onRevealCompleteRef_(LUA_NOREF)
    , onCharRevealedRef_(LUA_NOREF)
{
}

TextLayer::~TextLayer() {
    destroyGlyphLayers();
    if (plainText_) {
        allocator_->free(plainText_);
        plainText_ = nullptr;
    }
    if (lua_) {
        if (onRevealCompleteRef_ != LUA_NOREF) {
            luaL_unref(lua_, LUA_REGISTRYINDEX, onRevealCompleteRef_);
            onRevealCompleteRef_ = LUA_NOREF;
        }
        if (onCharRevealedRef_ != LUA_NOREF) {
            luaL_unref(lua_, LUA_REGISTRYINDEX, onCharRevealedRef_);
            onCharRevealedRef_ = LUA_NOREF;
        }
    }
}

void TextLayer::setFont(int fontHandle) {
    fontHandle_ = fontHandle;
}

void TextLayer::setPosition(float x, float y) {
    posX_ = x;
    posY_ = y;
}

void TextLayer::setSize(float pointSize) {
    pointSize_ = pointSize;
}

void TextLayer::setColor(float r, float g, float b, float a) {
    colorR_ = r; colorG_ = g; colorB_ = b; colorA_ = a;
    // Update existing visible glyph layers
    for (auto& gl : glyphLayers_) {
        if (gl.vectorLayerId >= 0) {
            gl.baseR = r; gl.baseG = g; gl.baseB = b; gl.baseA = a;
            if (gl.revealed) {
                renderer_->setVectorLayerColor(gl.vectorLayerId, r, g, b, a);
            }
        }
    }
}

void TextLayer::setWrapWidth(float width) {
    wrapWidth_ = width;
}

void TextLayer::setLineSpacing(float mult) {
    lineSpacingMult_ = mult;
}

void TextLayer::setAlignment(int align) {
    alignment_ = align;
}

void TextLayer::setFontFamily(int boldHandle, int italicHandle, int boldItalicHandle) {
    fontFamilyBold_       = boldHandle;
    fontFamilyItalic_     = italicHandle;
    fontFamilyBoldItalic_ = boldItalicHandle;
}

// ---------------------------------------------------------------------------
// Simple markup parser
// ---------------------------------------------------------------------------
// Supported tags (case-insensitive):
//   [color=RRGGBBAA]...[/color]
//   [wave amp=N freq=N]...[/wave]
//   [shake mag=N]...[/shake]
//   [rainbow speed=N]...[/rainbow]
//   [scale=N]...[/scale]
//
// Returns plain text (markup stripped) in plainText_, spans in spans_.
// ---------------------------------------------------------------------------

static bool iTagMatch(const char* tag, const char* name) {
    while (*name) {
        if (SDL_tolower(*tag) != SDL_tolower(*name)) return false;
        tag++; name++;
    }
    return *tag == '\0' || *tag == ' ' || *tag == '=' || *tag == ']';
}

static float parseFloat(const char* s) {
    // Simple ASCII float parse: [+-] digit* [. digit*]
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    else if (*s == '+') { s++; }
    float val = 0.0f;
    while (*s >= '0' && *s <= '9') { val = val * 10.0f + (*s++ - '0'); }
    if (*s == '.') {
        s++;
        float frac = 0.1f;
        while (*s >= '0' && *s <= '9') { val += (*s++ - '0') * frac; frac *= 0.1f; }
    }
    return neg ? -val : val;
}

static Uint32 parseHex8(const char* s) {
    // Parse exactly 8 hex digits as Uint32 (RRGGBBAA)
    Uint32 v = 0;
    for (int i = 0; i < 8; i++) {
        char c = s[i];
        int d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return 0;
        v = (v << 4) | (Uint32)d;
    }
    return v;
}

static const char* skipAttr(const char* p, const char* name, float* out) {
    // Try to parse name=value at p; return pointer after it, or nullptr.
    while (*p == ' ') p++;
    size_t nl = 0;
    const char* n = name;
    while (*n) { nl++; n++; }
    for (size_t i = 0; i < nl; i++) {
        if (SDL_tolower(p[i]) != SDL_tolower(name[i])) return nullptr;
    }
    const char* q = p + nl;
    while (*q == ' ') q++;
    if (*q != '=') return nullptr;
    q++;
    while (*q == ' ') q++;
    if (out) *out = parseFloat(q);
    while (*q && *q != ' ' && *q != ']') q++;
    return q;
}

void TextLayer::parseMarkup(const char* raw) {
    spans_.clear();

    // We build the plain text into a temporary buffer, then copy it.
    // Max length = strlen(raw) + 1 (markup always makes it shorter).
    int rawLen = 0;
    while (raw[rawLen]) rawLen++;
    char* buf = static_cast<char*>(allocator_->allocate((Uint64)rawLen + 1, "TextLayer::parseMarkup::buf"));
    assert(buf != nullptr);

    int    plainLen = 0;
    const char* p  = raw;

    struct OpenSpan { int startChar; MarkupEffect effect; float params[4]; int fontHandle; };
    // Use a small fixed stack (8 nested tags is more than enough)
    OpenSpan stack[8];
    int stackTop = 0;

    while (*p) {
        if (*p != '[') {
            buf[plainLen++] = *p++;
            continue;
        }
        // Try to parse a tag
        const char* tagStart = p + 1;
        const char* tagEnd   = tagStart;
        while (*tagEnd && *tagEnd != ']') tagEnd++;
        if (*tagEnd != ']') {
            // Not a valid tag; copy literal '['
            buf[plainLen++] = *p++;
            continue;
        }
        // tagStart..tagEnd is the tag content (not including [ ])
        const char* t = tagStart;

        bool isClose = (*t == '/');
        if (isClose) t++;

        MarkupEffect effect = MARKUP_EFFECT_NONE;
        float        params[4] = {1.0f, 1.0f, 0.0f, 1.0f};
        int          spanFontHandle = 0; // for MARKUP_EFFECT_FONT

        // --- Identify effect type ---
        if (iTagMatch(t, "color")) {
            effect = MARKUP_EFFECT_COLOR;
            if (!isClose) {
                const char* eq = t;
                while (*eq && *eq != '=' && *eq != ']') eq++;
                if (*eq == '=') {
                    eq++;
                    Uint32 hex = parseHex8(eq);
                    params[0] = ((hex >> 24) & 0xFF) / 255.0f;
                    params[1] = ((hex >> 16) & 0xFF) / 255.0f;
                    params[2] = ((hex >>  8) & 0xFF) / 255.0f;
                    params[3] = ((hex)       & 0xFF) / 255.0f;
                }
            }
        } else if (iTagMatch(t, "wave")) {
            effect = MARKUP_EFFECT_WAVE;
            if (!isClose) {
                const char* q = t + 4;  // skip "wave"
                const char* r;
                if ((r = skipAttr(q, "amp",  &params[0]))) q = r;
                if ((r = skipAttr(q, "freq", &params[1]))) q = r;
            }
        } else if (iTagMatch(t, "shake")) {
            effect = MARKUP_EFFECT_SHAKE;
            if (!isClose) {
                const char* q = t + 5;  // skip "shake"
                const char* r;
                if ((r = skipAttr(q, "mag", &params[0]))) q = r;
            }
        } else if (iTagMatch(t, "rainbow")) {
            effect = MARKUP_EFFECT_RAINBOW;
            if (!isClose) {
                const char* q = t + 7;  // skip "rainbow"
                const char* r;
                if ((r = skipAttr(q, "speed", &params[0]))) q = r;
            }
        } else if (iTagMatch(t, "scale")) {
            effect = MARKUP_EFFECT_SCALE;
            if (!isClose) {
                const char* eq = t + 5; // skip "scale"
                while (*eq == ' ') eq++;
                if (*eq == '=') { eq++; params[0] = parseFloat(eq); }
            }
        } else if (iTagMatch(t, "font")) {
            effect = MARKUP_EFFECT_FONT;
            if (!isClose) {
                // [font=bold], [font=italic], [font=bolditalic], or [font=N] (direct handle)
                const char* eq = t + 4; // skip "font"
                while (*eq == ' ') eq++;
                if (*eq == '=') {
                    eq++;
                    while (*eq == ' ') eq++;
                    if (SDL_strncasecmp(eq, "bold", 4) == 0 &&
                        (eq[4] == ']' || eq[4] == ' ')) {
                        spanFontHandle = fontFamilyBold_;
                    } else if (SDL_strncasecmp(eq, "italic", 6) == 0 &&
                               (eq[6] == ']' || eq[6] == ' ')) {
                        spanFontHandle = fontFamilyItalic_;
                    } else if (SDL_strncasecmp(eq, "bolditalic", 10) == 0 &&
                               (eq[10] == ']' || eq[10] == ' ')) {
                        spanFontHandle = fontFamilyBoldItalic_;
                    } else {
                        // Numeric font handle
                        spanFontHandle = (int)parseFloat(eq);
                    }
                    // Fall back to base font if the variant handle is invalid
                    if (spanFontHandle < 0 || !fontManager_->isValid(spanFontHandle)) {
                        spanFontHandle = fontHandle_;
                    }
                }
            }
        } else {
            // Unknown tag — treat as literal text
            buf[plainLen++] = '[';
            p++;
            continue;
        }

        if (!isClose) {
            if (stackTop < 8) {
                stack[stackTop].startChar  = plainLen;
                stack[stackTop].effect     = effect;
                stack[stackTop].fontHandle = spanFontHandle;
                for (int i = 0; i < 4; i++) stack[stackTop].params[i] = params[i];
                stackTop++;
            }
        } else {
            // Pop matching span
            for (int si = stackTop - 1; si >= 0; si--) {
                if (stack[si].effect == effect) {
                    MarkupSpan span{};
                    span.startChar  = stack[si].startChar;
                    span.endChar    = plainLen;
                    span.effect     = effect;
                    span.fontHandle = stack[si].fontHandle;
                    for (int i = 0; i < 4; i++) span.params[i] = stack[si].params[i];
                    spans_.push_back(span);
                    // Remove from stack
                    for (int k = si; k < stackTop - 1; k++) stack[k] = stack[k + 1];
                    stackTop--;
                    break;
                }
            }
        }

        p = tagEnd + 1;  // skip past ']'
    }

    buf[plainLen] = '\0';

    // Store plain text
    if (plainText_) {
        allocator_->free(plainText_);
        plainText_ = nullptr;
    }
    char* owned = static_cast<char*>(allocator_->allocate((Uint64)plainLen + 1, "TextLayer::plainText_"));
    assert(owned != nullptr);
    SDL_memcpy(owned, buf, (size_t)plainLen + 1);
    plainText_ = owned;

    allocator_->free(buf);
}

// ---------------------------------------------------------------------------
// setString / rebuild
// ---------------------------------------------------------------------------

void TextLayer::setString(const char* text, Uint64 sceneId) {
    parseMarkup(text ? text : "");
    sceneId_ = sceneId;
    revealAccum_ = 0.0f;
    revealComplete_ = false;
    time_       = 0.0f;
    rebuild(sceneId);
}

void TextLayer::rebuild(Uint64 sceneId) {
    destroyGlyphLayers();
    if (!plainText_ || plainText_[0] == '\0' || !fontManager_->isValid(fontHandle_)) {
        totalChars_  = 0;
        revealCount_ = 0;
        revealComplete_ = true;
        return;
    }

    TextLayout layout(allocator_, fontManager_);
    TextLayoutParams p{};
    p.fontHandle      = fontHandle_;
    p.originX         = posX_;
    p.originY         = posY_;
    p.pointSize       = pointSize_;
    p.wrapWidth       = wrapWidth_;
    p.lineSpacingMult = lineSpacingMult_;
    p.alignment       = alignment_;

    // Collect MARKUP_EFFECT_FONT spans to pass to the layout engine (M6)
    Vector<MarkupSpan> fontSpans(*allocator_, "TextLayer::rebuild::fontSpans");
    for (int si = 0; si < (int)spans_.size(); si++) {
        if (spans_[si].effect == MARKUP_EFFECT_FONT) {
            fontSpans.push_back(spans_[si]);
        }
    }
    p.fontSpans    = fontSpans.size() > 0 ? fontSpans.data() : nullptr;
    p.numFontSpans = (int)fontSpans.size();

    layout.layout(plainText_, p);

    int n = layout.getGlyphCount();
    totalChars_ = n;

    // Determine initial revealCount
    if (revealSpeed_ <= 0.0f) {
        revealCount_    = n;
        revealComplete_ = true;
    } else {
        revealCount_    = (int)revealAccum_;
        if (revealCount_ > n) revealCount_ = n;
        revealComplete_ = (revealCount_ >= n);
    }

    for (int i = 0; i < n; i++) {
        const GlyphInstance& gi = layout.getGlyph(i);

        GlyphLayerInfo info{};
        info.baseX = gi.x;
        info.baseY = gi.y;
        info.baseR = colorR_;
        info.baseG = colorG_;
        info.baseB = colorB_;
        info.baseA = colorA_;
        info.revealTimer = -1.0f;  // not yet revealed
        info.charIndex   = gi.charIndex;
        info.revealed    = false;

        // Apply static markup effects (COLOR, SCALE)
        for (int si = 0; si < (int)spans_.size(); si++) {
            const MarkupSpan& sp = spans_[si];
            if (gi.charIndex < sp.startChar || gi.charIndex >= sp.endChar) continue;
            if (sp.effect == MARKUP_EFFECT_COLOR) {
                info.baseR = sp.params[0];
                info.baseG = sp.params[1];
                info.baseB = sp.params[2];
                info.baseA = sp.params[3];
            }
        }        if (!gi.hasOutline) {
            info.vectorLayerId = -1;
            info.revealed      = true;  // invisible anyway
            glyphLayers_.push_back(info);
            continue;
        }

        Uint64 shapeId = fontManager_->getGlyphShapeId(gi.fontHandle, gi.glyphIndex);
        if (shapeId == 0) {
            info.vectorLayerId = -1;
            info.revealed      = true;
            glyphLayers_.push_back(info);
            continue;
        }

        // Compute scale (for MARKUP_EFFECT_SCALE)
        float layerScale = gi.scale;
        for (int si = 0; si < (int)spans_.size(); si++) {
            const MarkupSpan& sp = spans_[si];
            if (gi.charIndex >= sp.startChar && gi.charIndex < sp.endChar
                && sp.effect == MARKUP_EFFECT_SCALE) {
                layerScale *= sp.params[0];
            }
        }

        // Initially hidden (alpha=0) if not yet revealed
        float initA = (i < revealCount_) ? info.baseA : 0.0f;
        info.revealed = (i < revealCount_);
        if (info.revealed) info.revealTimer = FADE_IN_TIME + 1.0f;

        int lid = renderer_->createVectorLayer(shapeId, sceneId,
                                               gi.x, gi.y,
                                               layerScale * (float)fontManager_->getUnitsPerEM(gi.fontHandle),
                                               info.baseR, info.baseG, info.baseB, initA);
        info.vectorLayerId = lid;
        glyphLayers_.push_back(info);
    }
}

void TextLayer::setRevealSpeed(float charsPerSecond) {
    revealSpeed_ = charsPerSecond;
    if (revealSpeed_ <= 0.0f) {
        // Instant: reveal everything
        revealCount_    = totalChars_;
        revealComplete_ = true;
        for (int i = 0; i < (int)glyphLayers_.size(); i++) {
            GlyphLayerInfo& gl = glyphLayers_[i];
            if (!gl.revealed && gl.vectorLayerId >= 0) {
                renderer_->setVectorLayerColor(gl.vectorLayerId,
                    gl.baseR, gl.baseG, gl.baseB, gl.baseA);
            }
            gl.revealed = true;
        }
    }
}

void TextLayer::setRevealCount(int n) {
    if (n < 0) n = 0;
    if (n > totalChars_) n = totalChars_;
    // Ensure hidden chars up to n are shown
    for (int i = revealCount_; i < n; i++) {
        if (i < (int)glyphLayers_.size()) {
            GlyphLayerInfo& gl = glyphLayers_[i];
            gl.revealed    = true;
            gl.revealTimer = FADE_IN_TIME + 1.0f;
            if (gl.vectorLayerId >= 0) {
                renderer_->setVectorLayerColor(gl.vectorLayerId,
                    gl.baseR, gl.baseG, gl.baseB, gl.baseA);
            }
        }
    }
    revealCount_    = n;
    revealComplete_ = (n >= totalChars_);
}

void TextLayer::setOnRevealComplete(lua_State* L, int funcRef) {
    if (lua_ && onRevealCompleteRef_ != LUA_NOREF) {
        luaL_unref(lua_, LUA_REGISTRYINDEX, onRevealCompleteRef_);
    }
    lua_ = L;
    onRevealCompleteRef_ = funcRef;
}

void TextLayer::setOnCharRevealed(lua_State* L, int funcRef) {
    if (lua_ && onCharRevealedRef_ != LUA_NOREF) {
        luaL_unref(lua_, LUA_REGISTRYINDEX, onCharRevealedRef_);
    }
    lua_ = L;
    onCharRevealedRef_ = funcRef;
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------

void TextLayer::update(float dt, Uint64 sceneId) {
    (void)sceneId;
    time_ += dt;
    updateReveal(dt, sceneId);
    applyEffects(dt);
}

void TextLayer::updateReveal(float dt, Uint64 /*sceneId*/) {
    if (revealComplete_ || revealSpeed_ <= 0.0f) {
        // Instant reveal — ensure any fade-in timers run.
        for (auto& gl : glyphLayers_) {
            if (gl.vectorLayerId < 0) continue;
            if (gl.revealTimer >= 0.0f && gl.revealTimer < FADE_IN_TIME) {
                gl.revealTimer += dt;
                float alpha = (FADE_IN_TIME > 0.0f)
                    ? SDL_clamp(gl.revealTimer / FADE_IN_TIME, 0.0f, 1.0f)
                    : 1.0f;
                renderer_->setVectorLayerColor(gl.vectorLayerId,
                    gl.baseR, gl.baseG, gl.baseB, gl.baseA * alpha);
            }
        }
        return;
    }

    revealAccum_ += dt * revealSpeed_;
    int newReveal = (int)revealAccum_;
    if (newReveal > totalChars_) newReveal = totalChars_;

    for (int i = revealCount_; i < newReveal && i < (int)glyphLayers_.size(); i++) {
        GlyphLayerInfo& gl = glyphLayers_[i];
        gl.revealed    = true;
        gl.revealTimer = 0.0f;  // start fade-in
        if (gl.vectorLayerId >= 0) {
            renderer_->setVectorLayerColor(gl.vectorLayerId,
                gl.baseR, gl.baseG, gl.baseB, 0.0f);  // start transparent
        }
        if (lua_ && onCharRevealedRef_ != LUA_NOREF) {
            lua_rawgeti(lua_, LUA_REGISTRYINDEX, onCharRevealedRef_);
            lua_pushinteger(lua_, i);
            lua_pcall(lua_, 1, 0, 0);
        }
    }

    revealCount_ = newReveal;

    if (revealCount_ >= totalChars_ && !revealComplete_) {
        revealComplete_ = true;
        if (lua_ && onRevealCompleteRef_ != LUA_NOREF) {
            lua_rawgeti(lua_, LUA_REGISTRYINDEX, onRevealCompleteRef_);
            lua_pcall(lua_, 0, 0, 0);
        }
    }

    // Advance fade-in timers for recently revealed glyphs
    for (int i = 0; i < (int)glyphLayers_.size(); i++) {
        GlyphLayerInfo& gl = glyphLayers_[i];
        if (gl.vectorLayerId < 0) continue;
        if (!gl.revealed) continue;
        if (gl.revealTimer >= 0.0f && gl.revealTimer < FADE_IN_TIME) {
            gl.revealTimer += dt;
            float alpha = (FADE_IN_TIME > 0.0f)
                ? SDL_clamp(gl.revealTimer / FADE_IN_TIME, 0.0f, 1.0f)
                : 1.0f;
            renderer_->setVectorLayerColor(gl.vectorLayerId,
                gl.baseR, gl.baseG, gl.baseB, gl.baseA * alpha);
        }
    }
}

// Simple hash for per-char stable random (for SHAKE effect)
static float stableRand(int charIdx, int seed, float time) {
    // Use a simple hash to produce pseudo-random values that don't change
    // between frames but do change roughly every 0.1s (flicker frequency).
    int t   = (int)(time * 10.0f);
    Uint32 h = (Uint32)(charIdx * 1664525 + seed * 1013904223 + t * 22695477);
    h ^= (h >> 16);
    h *= 0x45d9f3b;
    h ^= (h >> 16);
    // Map [0, 0xFFFFFFFF] to [-1, 1]
    return (float)(int)h / (float)0x7FFFFFFF;
}

// HSV → RGB (h in [0,1], s=1, v=1 for rainbow)
static void hsvToRgb(float h, float& r, float& g, float& b) {
    h = SDL_fmodf(h, 1.0f);
    if (h < 0.0f) h += 1.0f;
    float hh = h * 6.0f;
    int   i  = (int)hh;
    float ff = hh - (float)i;
    float q  = 1.0f - ff;
    switch (i % 6) {
    case 0: r = 1.0f; g = ff;   b = 0.0f; break;
    case 1: r = q;    g = 1.0f; b = 0.0f; break;
    case 2: r = 0.0f; g = 1.0f; b = ff;   break;
    case 3: r = 0.0f; g = q;    b = 1.0f; break;
    case 4: r = ff;   g = 0.0f; b = 1.0f; break;
    default:r = 1.0f; g = 0.0f; b = q;    break;
    }
}

void TextLayer::applyEffects(float /*dt*/) {
    if (spans_.empty()) return;
    if (glyphLayers_.empty()) return;

    for (int si = 0; si < (int)spans_.size(); si++) {
        const MarkupSpan& sp = spans_[si];
        if (sp.effect == MARKUP_EFFECT_NONE ||
            sp.effect == MARKUP_EFFECT_COLOR ||
            sp.effect == MARKUP_EFFECT_SCALE  ||
            sp.effect == MARKUP_EFFECT_FONT) continue;  // static, set at rebuild

        for (int gi = 0; gi < (int)glyphLayers_.size(); gi++) {
            GlyphLayerInfo& gl = glyphLayers_[gi];
            if (gl.vectorLayerId < 0) continue;
            if (!gl.revealed) continue;

            int charIdx = gl.charIndex;
            if (charIdx < sp.startChar || charIdx >= sp.endChar) continue;

            if (sp.effect == MARKUP_EFFECT_WAVE) {
                float amp  = sp.params[0];
                float freq = sp.params[1];
                float yOff = amp * SDL_sinf(time_ * freq + (float)charIdx * (SDL_PI_F / 4.0f));
                renderer_->setVectorLayerPosition(gl.vectorLayerId,
                    gl.baseX, gl.baseY + yOff);

            } else if (sp.effect == MARKUP_EFFECT_SHAKE) {
                float mag = sp.params[0];
                float xOff = mag * stableRand(charIdx, 1, time_);
                float yOff = mag * stableRand(charIdx, 2, time_);
                renderer_->setVectorLayerPosition(gl.vectorLayerId,
                    gl.baseX + xOff, gl.baseY + yOff);

            } else if (sp.effect == MARKUP_EFFECT_RAINBOW) {
                float speed = sp.params[0];
                float hue   = SDL_fmodf(time_ * speed + (float)charIdx * 0.15f, 1.0f);
                float r, g, b;
                hsvToRgb(hue, r, g, b);
                // Preserve alpha fade-in
                float alpha = gl.baseA;
                if (gl.revealTimer >= 0.0f && gl.revealTimer < FADE_IN_TIME && FADE_IN_TIME > 0.0f) {
                    alpha *= SDL_clamp(gl.revealTimer / FADE_IN_TIME, 0.0f, 1.0f);
                }
                renderer_->setVectorLayerColor(gl.vectorLayerId, r, g, b, alpha);
            }
        }
    }
}

void TextLayer::destroyGlyphLayers() {
    for (auto& gl : glyphLayers_) {
        if (gl.vectorLayerId >= 0) {
            renderer_->destroyVectorLayer(gl.vectorLayerId);
            gl.vectorLayerId = -1;
        }
    }
    glyphLayers_.clear();
    totalChars_     = 0;
    revealCount_    = 0;
    revealComplete_ = true;
}
