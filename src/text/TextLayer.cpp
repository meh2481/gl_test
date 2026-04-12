#include "TextLayer.h"
#include "FontManager.h"
#include "../vulkan/VulkanRenderer.h"
#include "../debug/ConsoleBuffer.h"
#include "../core/ResourceTypes.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <cstring>

// Per-character fade-in duration in seconds.
const float TextLayer::FADE_IN_TIME = 0.08f;

// Number of floats per vertex in the text pipeline vertex buffer.
static const int TEXT_FLOATS_PER_VERTEX = 11;
// Number of vertices per glyph quad (2 triangles).
static const int TEXT_VERTS_PER_GLYPH = 6;

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
    , shadowEnabled_(false)
    , shadowDX_(0.0f), shadowDY_(0.0f)
    , shadowR_(0.0f), shadowG_(0.0f), shadowB_(0.0f), shadowA_(0.8f)
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
    , textLayerGpuId_(-1)
    , numSdfGlyphs_(0)
    , totalSdfVertices_(0)
    , mainVertOffset_(0)
    , cpuVertices_(*allocator, "TextLayer::cpuVertices_")
    , verticesDirty_(false)
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

// ---------------------------------------------------------------------------
// Configuration setters
// ---------------------------------------------------------------------------

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
    if (textLayerGpuId_ < 0) return;

    for (auto& gl : glyphLayers_) {
        gl.baseR = r; gl.baseG = g; gl.baseB = b; gl.baseA = a;
        if (!gl.hasOutline || gl.sdfGlyphIdx < 0 || !gl.revealed) continue;

        float alpha = a;
        if (gl.revealTimer >= 0.0f && gl.revealTimer < FADE_IN_TIME && FADE_IN_TIME > 0.0f) {
            alpha *= SDL_clamp(gl.revealTimer / FADE_IN_TIME, 0.0f, 1.0f);
        }
        int mainIdx = (mainVertOffset_ + gl.sdfGlyphIdx) * TEXT_VERTS_PER_GLYPH;
        updateGlyphQuadColor(cpuVertices_.data(), mainIdx, r, g, b, alpha);
        verticesDirty_ = true;
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

void TextLayer::setShadow(float dx, float dy, float r, float g, float b, float a) {
    shadowEnabled_ = true;
    shadowDX_ = dx; shadowDY_ = dy;
    shadowR_ = r; shadowG_ = g; shadowB_ = b; shadowA_ = a;
    if (textLayerGpuId_ >= 0 && plainText_) rebuild(sceneId_);
}

void TextLayer::clearShadow() {
    shadowEnabled_ = false;
    if (textLayerGpuId_ >= 0 && plainText_) rebuild(sceneId_);
}

// ---------------------------------------------------------------------------
// Simple markup parser
// ---------------------------------------------------------------------------

static bool iTagMatch(const char* tag, const char* name) {
    while (*name) {
        if (SDL_tolower(*tag) != SDL_tolower(*name)) return false;
        tag++; name++;
    }
    return *tag == '\0' || *tag == ' ' || *tag == '=' || *tag == ']';
}

static float parseFloat(const char* s) {
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

    int rawLen = 0;
    while (raw[rawLen]) rawLen++;
    char* buf = static_cast<char*>(allocator_->allocate((Uint64)rawLen + 1, "TextLayer::parseMarkup::buf"));
    assert(buf != nullptr);

    int    plainLen  = 0;
    int    charCount = 0; // codepoint counter — matches TextLayout's charIdx
    const char* p  = raw;

    struct OpenSpan { int startChar; MarkupEffect effect; float params[4]; int fontHandle; };
    OpenSpan stack[8];
    int stackTop = 0;

    while (*p) {
        if (*p != '[') {
            unsigned char uc = (unsigned char)*p;
            buf[plainLen++] = *p++;
            // Count codepoints: skip UTF-8 continuation bytes (0x80–0xBF)
            if ((uc & 0xC0) != 0x80) charCount++;
            continue;
        }
        const char* tagStart = p + 1;
        const char* tagEnd   = tagStart;
        while (*tagEnd && *tagEnd != ']') tagEnd++;
        if (*tagEnd != ']') {
            buf[plainLen++] = *p++;
            charCount++; // '[' is ASCII, always a codepoint
            continue;
        }
        const char* t = tagStart;

        bool isClose = (*t == '/');
        if (isClose) t++;

        MarkupEffect effect = MARKUP_EFFECT_NONE;
        float        params[4] = {1.0f, 1.0f, 0.0f, 1.0f};
        int          spanFontHandle = 0;

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
                const char* q = t + 4;
                const char* r;
                if ((r = skipAttr(q, "amp",  &params[0]))) q = r;
                if ((r = skipAttr(q, "freq", &params[1]))) q = r;
            }
        } else if (iTagMatch(t, "shake")) {
            effect = MARKUP_EFFECT_SHAKE;
            if (!isClose) {
                const char* q = t + 5;
                const char* r;
                if ((r = skipAttr(q, "mag", &params[0]))) q = r;
            }
        } else if (iTagMatch(t, "rainbow")) {
            effect = MARKUP_EFFECT_RAINBOW;
            if (!isClose) {
                const char* q = t + 7;
                const char* r;
                if ((r = skipAttr(q, "speed", &params[0]))) q = r;
            }
        } else if (iTagMatch(t, "scale")) {
            effect = MARKUP_EFFECT_SCALE;
            if (!isClose) {
                const char* eq = t + 5;
                while (*eq == ' ') eq++;
                if (*eq == '=') { eq++; params[0] = parseFloat(eq); }
            }
        } else if (iTagMatch(t, "font")) {
            effect = MARKUP_EFFECT_FONT;
            if (!isClose) {
                const char* eq = t + 4;
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
                        spanFontHandle = (int)parseFloat(eq);
                    }
                    if (spanFontHandle < 0 || !fontManager_->isValid(spanFontHandle)) {
                        spanFontHandle = fontHandle_;
                    }
                }
            }
        } else {
            buf[plainLen++] = '[';
            charCount++; // '[' is ASCII, always a codepoint
            p++;
            continue;
        }

        if (!isClose) {
            if (stackTop < 8) {
                stack[stackTop].startChar  = charCount;
                stack[stackTop].effect     = effect;
                stack[stackTop].fontHandle = spanFontHandle;
                for (int i = 0; i < 4; i++) stack[stackTop].params[i] = params[i];
                stackTop++;
            }
        } else {
            for (int si = stackTop - 1; si >= 0; si--) {
                if (stack[si].effect == effect) {
                    MarkupSpan span{};
                    span.startChar  = stack[si].startChar;
                    span.endChar    = charCount;
                    span.effect     = effect;
                    span.fontHandle = stack[si].fontHandle;
                    for (int i = 0; i < 4; i++) span.params[i] = stack[si].params[i];
                    spans_.push_back(span);
                    for (int k = si; k < stackTop - 1; k++) stack[k] = stack[k + 1];
                    stackTop--;
                    break;
                }
            }
        }

        p = tagEnd + 1;
    }

    buf[plainLen] = '\0';

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
// Vertex buffer helpers
// ---------------------------------------------------------------------------

// Quad corner remapping (matches winding in vector_vertex.glsl):
//   corners 0=BL, 1=BR, 2=TL, 3=TR
//   triangle 1: BL,BR,TL  →  indices 0,1,2
//   triangle 2: BR,TR,TL  →  indices 1,3,2
static const int QUAD_CORNER_REMAP[6] = {0, 1, 2, 1, 3, 2};
static const int QUAD_CORNER_XSEL[4]  = {0, 1, 0, 1};  // 0→minX, 1→maxX
static const int QUAD_CORNER_YSEL[4]  = {0, 0, 1, 1};  // 0→minY, 1→maxY

/*static*/ void TextLayer::writeGlyphQuad(float* buf, int vertStartIdx, int glyphDescIdx,
                                          float worldOriginX, float worldOriginY,
                                          float bboxMinX, float bboxMinY,
                                          float bboxMaxX, float bboxMaxY,
                                          float glyphScale,
                                          float r, float g, float b, float a)
{
    Uint32 gi = (Uint32)glyphDescIdx;
    float bboxX[2] = {bboxMinX, bboxMaxX};
    float bboxY[2] = {bboxMinY, bboxMaxY};

    float* base = buf + vertStartIdx * TEXT_FLOATS_PER_VERTEX;
    for (int v = 0; v < TEXT_VERTS_PER_GLYPH; v++) {
        int corner = QUAD_CORNER_REMAP[v];
        float lx = bboxX[QUAD_CORNER_XSEL[corner]];
        float ly = bboxY[QUAD_CORNER_YSEL[corner]];
        float wx = worldOriginX + lx * glyphScale;
        float wy = worldOriginY + ly * glyphScale;

        float* vp = base + v * TEXT_FLOATS_PER_VERTEX;
        vp[0] = wx; vp[1] = wy;
        vp[2] = lx; vp[3] = ly;
        SDL_memcpy(&vp[4], &gi, sizeof(float));  // store uint32 as float bits
        vp[5] = r; vp[6] = g; vp[7] = b; vp[8] = a;
        vp[9]  = 0.0f;
        vp[10] = 0.0f;
    }
}

/*static*/ void TextLayer::updateGlyphQuadColor(float* buf, int vertStartIdx,
                                                 float r, float g, float b, float a)
{
    float* base = buf + vertStartIdx * TEXT_FLOATS_PER_VERTEX;
    for (int v = 0; v < TEXT_VERTS_PER_GLYPH; v++) {
        float* vp = base + v * TEXT_FLOATS_PER_VERTEX;
        vp[5] = r; vp[6] = g; vp[7] = b; vp[8] = a;
    }
}

/*static*/ void TextLayer::updateGlyphQuadOffset(float* buf, int vertStartIdx,
                                                  float xOff, float yOff)
{
    float* base = buf + vertStartIdx * TEXT_FLOATS_PER_VERTEX;
    for (int v = 0; v < TEXT_VERTS_PER_GLYPH; v++) {
        float* vp = base + v * TEXT_FLOATS_PER_VERTEX;
        vp[9]  = xOff;
        vp[10] = yOff;
    }
}

// ---------------------------------------------------------------------------
// setString / rebuild
// ---------------------------------------------------------------------------

void TextLayer::setString(const char* text, Uint64 sceneId) {
    parseMarkup(text ? text : "");
    sceneId_        = sceneId;
    revealAccum_    = 0.0f;
    revealComplete_ = false;
    time_           = 0.0f;
    rebuild(sceneId);
}

void TextLayer::rebuild(Uint64 sceneId) {
    destroyGlyphLayers();
    if (!plainText_ || plainText_[0] == '\0' || !fontManager_->isValid(fontHandle_)) {
        totalChars_     = 0;
        revealCount_    = 0;
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

    Vector<MarkupSpan> fontSpans(*allocator_, "TextLayer::rebuild::fontSpans");
    for (int si = 0; si < (int)spans_.size(); si++) {
        if (spans_[si].effect == MARKUP_EFFECT_FONT) fontSpans.push_back(spans_[si]);
    }
    p.fontSpans    = fontSpans.size() > 0 ? fontSpans.data() : nullptr;
    p.numFontSpans = (int)fontSpans.size();

    layout.layout(plainText_, p);

    int n = layout.getGlyphCount();
    totalChars_ = n;

    if (revealSpeed_ <= 0.0f) {
        revealCount_    = n;
        revealComplete_ = true;
    } else {
        revealCount_ = (int)revealAccum_;
        if (revealCount_ > n) revealCount_ = n;
        revealComplete_ = (revealCount_ >= n);
    }

    // ------------------------------------------------------------------
    // Pass 1: build GlyphLayerInfo list and per-string flat SDF arrays.
    // ------------------------------------------------------------------

    struct UniqueGlyph {
        int    fontHandle;
        Uint32 glyphIndex;
        Uint32 contourOffset;
        Uint32 numContours;
        float  bboxMinX, bboxMinY, bboxMaxX, bboxMaxY;
    };

    Vector<UniqueGlyph>      uniqueGlyphs (*allocator_, "TextLayer::rebuild::uniqueGlyphs");
    Vector<SdfContourHeader> flatContours (*allocator_, "TextLayer::rebuild::flatContours");
    Vector<SdfSegment>       flatSegments (*allocator_, "TextLayer::rebuild::flatSegments");
    Vector<Uint32>           glyphDescData(*allocator_, "TextLayer::rebuild::glyphDescData");

    numSdfGlyphs_ = 0;

    for (int i = 0; i < n; i++) {
        const GlyphInstance& gi = layout.getGlyph(i);

        GlyphLayerInfo info{};
        info.baseX       = gi.x;
        info.baseY       = gi.y;
        info.baseR       = colorR_;
        info.baseG       = colorG_;
        info.baseB       = colorB_;
        info.baseA       = colorA_;
        info.revealTimer = -1.0f;
        info.charIndex   = gi.charIndex;
        info.revealed    = (i < revealCount_);
        info.hasOutline  = gi.hasOutline;
        info.sdfGlyphIdx = -1;

        for (int si = 0; si < (int)spans_.size(); si++) {
            const MarkupSpan& sp = spans_[si];
            if (gi.charIndex < sp.startChar || gi.charIndex >= sp.endChar) continue;
            if (sp.effect == MARKUP_EFFECT_COLOR) {
                info.baseR = sp.params[0]; info.baseG = sp.params[1];
                info.baseB = sp.params[2]; info.baseA = sp.params[3];
            }
        }

        if (!gi.hasOutline) {
            info.revealed = true;
            glyphLayers_.push_back(info);
            continue;
        }

        float layerScale = gi.scale;
        for (int si = 0; si < (int)spans_.size(); si++) {
            const MarkupSpan& sp = spans_[si];
            if (gi.charIndex >= sp.startChar && gi.charIndex < sp.endChar
                && sp.effect == MARKUP_EFFECT_SCALE) {
                layerScale *= sp.params[0];
            }
        }
        float glyphScale = layerScale * (float)fontManager_->getUnitsPerEM(gi.fontHandle);

        Uint32 contourOffset = 0, numContours = 0;
        float  bboxMinX = 0, bboxMinY = 0, bboxMaxX = 0, bboxMaxY = 0;
        bool   found = false;

        for (Uint32 k = 0; k < (Uint32)uniqueGlyphs.size(); k++) {
            if (uniqueGlyphs[k].fontHandle == gi.fontHandle &&
                uniqueGlyphs[k].glyphIndex == gi.glyphIndex) {
                contourOffset = uniqueGlyphs[k].contourOffset;
                numContours   = uniqueGlyphs[k].numContours;
                bboxMinX = uniqueGlyphs[k].bboxMinX; bboxMinY = uniqueGlyphs[k].bboxMinY;
                bboxMaxX = uniqueGlyphs[k].bboxMaxX; bboxMaxY = uniqueGlyphs[k].bboxMaxY;
                found = true;
                break;
            }
        }

        if (!found) {
            Uint32 blobSize = 0;
            const char* sdfBlob = fontManager_->getGlyphSdfData(gi.fontHandle, gi.glyphIndex, &blobSize);
            if (!sdfBlob || blobSize < sizeof(SdfShapeHeader)) {
                info.hasOutline = false;
                info.revealed   = true;
                glyphLayers_.push_back(info);
                continue;
            }

            const SdfShapeHeader*   hdr = reinterpret_cast<const SdfShapeHeader*>(sdfBlob);
            const SdfContourHeader* contoursP =
                reinterpret_cast<const SdfContourHeader*>(sdfBlob + sizeof(SdfShapeHeader));
            const SdfSegment* segsP =
                reinterpret_cast<const SdfSegment*>(sdfBlob
                    + sizeof(SdfShapeHeader)
                    + hdr->numContours * sizeof(SdfContourHeader));

            contourOffset = (Uint32)flatContours.size();
            numContours   = hdr->numContours;
            Uint32 segBase = (Uint32)flatSegments.size();

            for (Uint32 ci = 0; ci < hdr->numContours; ci++) {
                SdfContourHeader c = contoursP[ci];
                c.segmentOffset += segBase;
                flatContours.push_back(c);
            }
            for (Uint32 si = 0; si < hdr->totalSegments; si++) {
                flatSegments.push_back(segsP[si]);
            }

            UniqueGlyph ug{};
            ug.fontHandle    = gi.fontHandle;
            ug.glyphIndex    = gi.glyphIndex;
            ug.contourOffset = contourOffset;
            ug.numContours   = numContours;
            ug.bboxMinX = hdr->bboxMinX; ug.bboxMinY = hdr->bboxMinY;
            ug.bboxMaxX = hdr->bboxMaxX; ug.bboxMaxY = hdr->bboxMaxY;
            uniqueGlyphs.push_back(ug);

            bboxMinX = hdr->bboxMinX; bboxMinY = hdr->bboxMinY;
            bboxMaxX = hdr->bboxMaxX; bboxMaxY = hdr->bboxMaxY;
        }

        if (info.revealed) info.revealTimer = FADE_IN_TIME + 1.0f;

        info.sdfGlyphIdx = numSdfGlyphs_;
        info.bboxMinX = bboxMinX; info.bboxMinY = bboxMinY;
        info.bboxMaxX = bboxMaxX; info.bboxMaxY = bboxMaxY;
        info.glyphScale = glyphScale;

        glyphDescData.push_back(contourOffset);
        glyphDescData.push_back(numContours);
        glyphDescData.push_back(0);
        glyphDescData.push_back(0);

        numSdfGlyphs_++;
        glyphLayers_.push_back(info);
    }

    // ------------------------------------------------------------------
    // Pass 2: build CPU vertex buffer.
    // Layout: [shadow_quads 0..n-1] [main_quads 0..n-1]  (or just [main_quads] if no shadow)
    // ------------------------------------------------------------------
    mainVertOffset_   = shadowEnabled_ ? numSdfGlyphs_ : 0;
    int shadowMult    = shadowEnabled_ ? 2 : 1;
    totalSdfVertices_ = numSdfGlyphs_ * TEXT_VERTS_PER_GLYPH * shadowMult;

    cpuVertices_.clear();
    if (totalSdfVertices_ > 0) {
        cpuVertices_.resize((Uint64)(totalSdfVertices_ * TEXT_FLOATS_PER_VERTEX), 0.0f);

        for (const GlyphLayerInfo& gl : glyphLayers_) {
            if (!gl.hasOutline || gl.sdfGlyphIdx < 0) continue;

            int si    = gl.sdfGlyphIdx;
            float initA = gl.revealed ? gl.baseA : 0.0f;

            int mainVertIdx = (mainVertOffset_ + si) * TEXT_VERTS_PER_GLYPH;
            writeGlyphQuad(cpuVertices_.data(), mainVertIdx, si,
                           gl.baseX, gl.baseY,
                           gl.bboxMinX, gl.bboxMinY, gl.bboxMaxX, gl.bboxMaxY,
                           gl.glyphScale,
                           gl.baseR, gl.baseG, gl.baseB, initA);

            if (shadowEnabled_) {
                float shadowInitA = gl.revealed ? shadowA_ : 0.0f;
                int shadowVertIdx = si * TEXT_VERTS_PER_GLYPH;
                writeGlyphQuad(cpuVertices_.data(), shadowVertIdx, si,
                               gl.baseX + shadowDX_, gl.baseY + shadowDY_,
                               gl.bboxMinX, gl.bboxMinY, gl.bboxMaxX, gl.bboxMaxY,
                               gl.glyphScale,
                               shadowR_, shadowG_, shadowB_, shadowInitA);
            }
        }
    }

    // ------------------------------------------------------------------
    // Pass 3: upload to GPU.
    // ------------------------------------------------------------------
    if (numSdfGlyphs_ > 0) {
        VkDeviceSize gdSize = (VkDeviceSize)glyphDescData.size() * sizeof(Uint32);
        VkDeviceSize cSize  = (VkDeviceSize)flatContours.size()  * sizeof(SdfContourHeader);
        VkDeviceSize sSize  = (VkDeviceSize)flatSegments.size()  * sizeof(SdfSegment);

        textLayerGpuId_ = renderer_->createTextLayerGpu(sceneId,
            cpuVertices_.data(), totalSdfVertices_,
            glyphDescData.data(), gdSize,
            flatContours.data(),  cSize,
            flatSegments.data(),  sSize);
        sceneId_ = sceneId;
    }

    verticesDirty_ = false;
}

// ---------------------------------------------------------------------------
// Reveal
// ---------------------------------------------------------------------------

void TextLayer::setRevealSpeed(float charsPerSecond) {
    revealSpeed_ = charsPerSecond;
    if (revealSpeed_ <= 0.0f) {
        revealCount_    = totalChars_;
        revealComplete_ = true;
        if (textLayerGpuId_ < 0) return;
        for (auto& gl : glyphLayers_) {
            if (!gl.hasOutline || gl.sdfGlyphIdx < 0 || gl.revealed) continue;
            gl.revealed    = true;
            gl.revealTimer = FADE_IN_TIME + 1.0f;
            int mainIdx = (mainVertOffset_ + gl.sdfGlyphIdx) * TEXT_VERTS_PER_GLYPH;
            updateGlyphQuadColor(cpuVertices_.data(), mainIdx,
                gl.baseR, gl.baseG, gl.baseB, gl.baseA);
            if (shadowEnabled_) {
                int shadowIdx = gl.sdfGlyphIdx * TEXT_VERTS_PER_GLYPH;
                updateGlyphQuadColor(cpuVertices_.data(), shadowIdx,
                    shadowR_, shadowG_, shadowB_, shadowA_);
            }
            verticesDirty_ = true;
        }
        for (auto& gl : glyphLayers_) gl.revealed = true;
    }
}

void TextLayer::setRevealCount(int n) {
    if (n < 0) n = 0;
    if (n > totalChars_) n = totalChars_;
    for (int i = revealCount_; i < n; i++) {
        if (i < (int)glyphLayers_.size()) {
            GlyphLayerInfo& gl = glyphLayers_[i];
            gl.revealed    = true;
            gl.revealTimer = FADE_IN_TIME + 1.0f;
            if (gl.hasOutline && gl.sdfGlyphIdx >= 0 && textLayerGpuId_ >= 0) {
                int mainIdx = (mainVertOffset_ + gl.sdfGlyphIdx) * TEXT_VERTS_PER_GLYPH;
                updateGlyphQuadColor(cpuVertices_.data(), mainIdx,
                    gl.baseR, gl.baseG, gl.baseB, gl.baseA);
                if (shadowEnabled_) {
                    int shadowIdx = gl.sdfGlyphIdx * TEXT_VERTS_PER_GLYPH;
                    updateGlyphQuadColor(cpuVertices_.data(), shadowIdx,
                        shadowR_, shadowG_, shadowB_, shadowA_);
                }
                verticesDirty_ = true;
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
// update / updateReveal / applyEffects
// ---------------------------------------------------------------------------

void TextLayer::update(float dt, Uint64 sceneId) {
    (void)sceneId;
    time_ += dt;
    updateReveal(dt, sceneId);
    applyEffects(dt);

    if (verticesDirty_ && textLayerGpuId_ >= 0 && totalSdfVertices_ > 0) {
        renderer_->updateTextLayerVertices(textLayerGpuId_,
            cpuVertices_.data(), totalSdfVertices_);
        verticesDirty_ = false;
    }
}

void TextLayer::updateReveal(float dt, Uint64 /*sceneId*/) {
    if (textLayerGpuId_ < 0) return;

    if (revealComplete_ || revealSpeed_ <= 0.0f) {
        for (auto& gl : glyphLayers_) {
            if (!gl.hasOutline || gl.sdfGlyphIdx < 0) continue;
            if (!gl.revealed) continue;
            if (gl.revealTimer >= 0.0f && gl.revealTimer < FADE_IN_TIME) {
                gl.revealTimer += dt;
                float alpha = (FADE_IN_TIME > 0.0f)
                    ? SDL_clamp(gl.revealTimer / FADE_IN_TIME, 0.0f, 1.0f) : 1.0f;
                int mainIdx = (mainVertOffset_ + gl.sdfGlyphIdx) * TEXT_VERTS_PER_GLYPH;
                updateGlyphQuadColor(cpuVertices_.data(), mainIdx,
                    gl.baseR, gl.baseG, gl.baseB, gl.baseA * alpha);
                if (shadowEnabled_) {
                    int shadowIdx = gl.sdfGlyphIdx * TEXT_VERTS_PER_GLYPH;
                    updateGlyphQuadColor(cpuVertices_.data(), shadowIdx,
                        shadowR_, shadowG_, shadowB_, shadowA_ * alpha);
                }
                verticesDirty_ = true;
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
        gl.revealTimer = 0.0f;

        if (gl.hasOutline && gl.sdfGlyphIdx >= 0) {
            int mainIdx = (mainVertOffset_ + gl.sdfGlyphIdx) * TEXT_VERTS_PER_GLYPH;
            updateGlyphQuadColor(cpuVertices_.data(), mainIdx,
                gl.baseR, gl.baseG, gl.baseB, 0.0f);
            if (shadowEnabled_) {
                int shadowIdx = gl.sdfGlyphIdx * TEXT_VERTS_PER_GLYPH;
                updateGlyphQuadColor(cpuVertices_.data(), shadowIdx,
                    shadowR_, shadowG_, shadowB_, 0.0f);
            }
            verticesDirty_ = true;
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

    for (int i = 0; i < (int)glyphLayers_.size(); i++) {
        GlyphLayerInfo& gl = glyphLayers_[i];
        if (!gl.hasOutline || gl.sdfGlyphIdx < 0) continue;
        if (!gl.revealed) continue;
        if (gl.revealTimer >= 0.0f && gl.revealTimer < FADE_IN_TIME) {
            gl.revealTimer += dt;
            float alpha = (FADE_IN_TIME > 0.0f)
                ? SDL_clamp(gl.revealTimer / FADE_IN_TIME, 0.0f, 1.0f) : 1.0f;
            int mainIdx = (mainVertOffset_ + gl.sdfGlyphIdx) * TEXT_VERTS_PER_GLYPH;
            updateGlyphQuadColor(cpuVertices_.data(), mainIdx,
                gl.baseR, gl.baseG, gl.baseB, gl.baseA * alpha);
            if (shadowEnabled_) {
                int shadowIdx = gl.sdfGlyphIdx * TEXT_VERTS_PER_GLYPH;
                updateGlyphQuadColor(cpuVertices_.data(), shadowIdx,
                    shadowR_, shadowG_, shadowB_, shadowA_ * alpha);
            }
            verticesDirty_ = true;
        }
    }
}

static float stableRand(int charIdx, int seed, float time) {
    // LCG-style hash mixing for stable per-character, per-frame random values.
    // Multipliers are standard Numerical Recipes LCG constants.
    static const Uint32 LCG_A  = 1664525u;
    static const Uint32 LCG_B  = 1013904223u;
    static const Uint32 LCG_C  = 22695477u;
    int t   = (int)(time * 10.0f);
    Uint32 h = (Uint32)(charIdx * LCG_A + seed * LCG_B + t * LCG_C);
    h ^= (h >> 16);
    h *= 0x45d9f3b;
    h ^= (h >> 16);
    return (float)(int)h / (float)0x7FFFFFFF;
}

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
    if (textLayerGpuId_ < 0) return;

    // Reset all effect offsets to zero each frame before reapplying.
    for (const GlyphLayerInfo& gl : glyphLayers_) {
        if (!gl.hasOutline || gl.sdfGlyphIdx < 0 || !gl.revealed) continue;
        int si = gl.sdfGlyphIdx;
        updateGlyphQuadOffset(cpuVertices_.data(), (mainVertOffset_ + si) * TEXT_VERTS_PER_GLYPH, 0.0f, 0.0f);
        if (shadowEnabled_) {
            updateGlyphQuadOffset(cpuVertices_.data(), si * TEXT_VERTS_PER_GLYPH, 0.0f, 0.0f);
        }
    }

    bool anyEffect = false;
    for (int si = 0; si < (int)spans_.size(); si++) {
        const MarkupSpan& sp = spans_[si];
        if (sp.effect == MARKUP_EFFECT_NONE  ||
            sp.effect == MARKUP_EFFECT_COLOR  ||
            sp.effect == MARKUP_EFFECT_SCALE  ||
            sp.effect == MARKUP_EFFECT_FONT) continue;

        for (int gi = 0; gi < (int)glyphLayers_.size(); gi++) {
            GlyphLayerInfo& gl = glyphLayers_[gi];
            if (!gl.hasOutline || gl.sdfGlyphIdx < 0) continue;
            if (!gl.revealed) continue;
            if (gl.charIndex < sp.startChar || gl.charIndex >= sp.endChar) continue;

            int charIdx   = gl.charIndex;
            int sdfIdx    = gl.sdfGlyphIdx;
            int mainIdx   = (mainVertOffset_ + sdfIdx) * TEXT_VERTS_PER_GLYPH;
            int shadowIdx = sdfIdx * TEXT_VERTS_PER_GLYPH;

            if (sp.effect == MARKUP_EFFECT_WAVE) {
                float amp  = sp.params[0];
                float freq = sp.params[1];
                float yOff = amp * SDL_sinf(time_ * freq + (float)charIdx * (SDL_PI_F / 4.0f));
                updateGlyphQuadOffset(cpuVertices_.data(), mainIdx, 0.0f, yOff);
                if (shadowEnabled_) updateGlyphQuadOffset(cpuVertices_.data(), shadowIdx, 0.0f, yOff);
                anyEffect = true;

            } else if (sp.effect == MARKUP_EFFECT_SHAKE) {
                float mag  = sp.params[0];
                float xOff = mag * stableRand(charIdx, 1, time_);
                float yOff = mag * stableRand(charIdx, 2, time_);
                updateGlyphQuadOffset(cpuVertices_.data(), mainIdx, xOff, yOff);
                if (shadowEnabled_) updateGlyphQuadOffset(cpuVertices_.data(), shadowIdx, xOff, yOff);
                anyEffect = true;

            } else if (sp.effect == MARKUP_EFFECT_RAINBOW) {
                float speed = sp.params[0];
                float hue   = SDL_fmodf(time_ * speed + (float)charIdx * 0.15f, 1.0f);
                float r, g, b;
                hsvToRgb(hue, r, g, b);
                float alpha = gl.baseA;
                if (gl.revealTimer >= 0.0f && gl.revealTimer < FADE_IN_TIME && FADE_IN_TIME > 0.0f) {
                    alpha *= SDL_clamp(gl.revealTimer / FADE_IN_TIME, 0.0f, 1.0f);
                }
                updateGlyphQuadColor(cpuVertices_.data(), mainIdx, r, g, b, alpha);
                anyEffect = true;
            }
        }
    }

    if (anyEffect) verticesDirty_ = true;
}

// ---------------------------------------------------------------------------
// destroyGlyphLayers
// ---------------------------------------------------------------------------

void TextLayer::destroyGlyphLayers() {
    if (textLayerGpuId_ >= 0) {
        renderer_->destroyTextLayerGpu(textLayerGpuId_);
        textLayerGpuId_ = -1;
    }
    glyphLayers_.clear();
    cpuVertices_.clear();
    numSdfGlyphs_     = 0;
    totalSdfVertices_ = 0;
    mainVertOffset_   = 0;
    verticesDirty_    = false;
    totalChars_       = 0;
    revealCount_      = 0;
    revealComplete_   = true;
}

bool TextLayer::isRevealAnimComplete(int upToCharIndex) const {
    for (int i = 0; i < (int)glyphLayers_.size() && i < upToCharIndex; i++) {
        const GlyphLayerInfo& gl = glyphLayers_[i];
        if (gl.revealed && gl.revealTimer >= 0.0f && gl.revealTimer < FADE_IN_TIME) {
            return false;
        }
    }
    return true;
}

void TextLayer::updateFadesAndEffects(float dt, Uint64 sceneId) {
    (void)sceneId;
    time_ += dt;
    // Advance fade-in timers for already-revealed glyphs without touching
    // the reveal accumulator.
    if (textLayerGpuId_ >= 0) {
        for (auto& gl : glyphLayers_) {
            if (!gl.hasOutline || gl.sdfGlyphIdx < 0 || !gl.revealed) continue;
            if (gl.revealTimer >= 0.0f && gl.revealTimer < FADE_IN_TIME) {
                gl.revealTimer += dt;
                float alpha = (FADE_IN_TIME > 0.0f)
                    ? SDL_clamp(gl.revealTimer / FADE_IN_TIME, 0.0f, 1.0f) : 1.0f;
                int mainIdx = (mainVertOffset_ + gl.sdfGlyphIdx) * TEXT_VERTS_PER_GLYPH;
                updateGlyphQuadColor(cpuVertices_.data(), mainIdx,
                    gl.baseR, gl.baseG, gl.baseB, gl.baseA * alpha);
                if (shadowEnabled_) {
                    int shadowIdx = gl.sdfGlyphIdx * TEXT_VERTS_PER_GLYPH;
                    updateGlyphQuadColor(cpuVertices_.data(), shadowIdx,
                        shadowR_, shadowG_, shadowB_, shadowA_ * alpha);
                }
                verticesDirty_ = true;
            }
        }
    }
    applyEffects(dt);
    if (verticesDirty_ && textLayerGpuId_ >= 0 && totalSdfVertices_ > 0) {
        renderer_->updateTextLayerVertices(textLayerGpuId_,
            cpuVertices_.data(), totalSdfVertices_);
        verticesDirty_ = false;
    }
}
