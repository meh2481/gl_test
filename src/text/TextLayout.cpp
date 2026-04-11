#include "TextLayout.h"
#include "FontManager.h"
#include "../core/ResourceTypes.h"

TextLayout::TextLayout(MemoryAllocator* allocator, FontManager* fontManager)
    : glyphs_(*allocator, "TextLayout::glyphs_")
    , fontManager_(fontManager)
    , allocator_(allocator)
    , textByteLen_(0)
{
}

Uint32 TextLayout::decodeUtf8(const char** p) {
    const unsigned char* s = reinterpret_cast<const unsigned char*>(*p);
    Uint32 cp;
    if (s[0] < 0x80) {
        cp = s[0];
        *p += 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        if ((s[1] & 0xC0) != 0x80) { *p += 1; return 0xFFFD; }
        cp = ((Uint32)(s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *p += 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) { *p += 1; return 0xFFFD; }
        cp = ((Uint32)(s[0] & 0x0F) << 12) | ((Uint32)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *p += 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) {
            *p += 1; return 0xFFFD;
        }
        cp = ((Uint32)(s[0] & 0x07) << 18) | ((Uint32)(s[1] & 0x3F) << 12)
           | ((Uint32)(s[2] & 0x3F) << 6)  |  (s[3] & 0x3F);
        *p += 4;
    } else {
        *p += 1;
        return 0xFFFD;
    }
    return cp;
}

void TextLayout::layout(const char* text, const TextLayoutParams& params) {
    glyphs_.clear();
    textByteLen_ = 0;
    if (!text || text[0] == '\0') return;

    int fontHandle = params.fontHandle;
    if (!fontManager_->isValid(fontHandle)) return;

    Sint32 unitsPerEM  = fontManager_->getUnitsPerEM(fontHandle);
    Sint32 ascender    = fontManager_->getAscender(fontHandle);
    Sint32 descender   = fontManager_->getDescender(fontHandle);
    Sint32 lineGap     = fontManager_->getLineGap(fontHandle);

    if (unitsPerEM <= 0) return;

    float scale      = params.pointSize / (float)unitsPerEM;
    // Line height in world units (ascender - descender + lineGap) * scale * lineSpacingMult
    float lineHeight = (float)(ascender - descender + lineGap) * scale * params.lineSpacingMult;
    // Baseline Y for the first line: originY is the top of the em box, so
    // baseline = originY - ascender * scale
    float baselineY  = params.originY - (float)ascender * scale;
    float cursorX    = params.originX;
    float cursorY    = baselineY;

    // Measure the space character advance for tab stops
    float spaceAdv = 0.0f;
    const FontGlyphEntry* spaceGe = fontManager_->lookupGlyph(fontHandle, 0x20);
    if (spaceGe) spaceAdv = (float)spaceGe->advanceWidth * scale;
    if (spaceAdv <= 0.0f) spaceAdv = params.pointSize * 0.25f;
    float tabStop = spaceAdv * 4.0f;

    // --- Pre-scan to collect word boundaries for word-wrap ---
    // We do a two-pass approach:
    //   Pass 1: compute all glyph positions on a single infinite line.
    //   Pass 2: insert line breaks for wrap, then handle alignment.
    // For simplicity we do a single greedy word-wrap pass inline.

    // Each glyph placed on the current line; line starts are tracked for alignment.
    struct LineStart { int glyphIdx; float startX; };
    Vector<LineStart> lineStarts(*allocator_, "TextLayout::lineStarts");
    lineStarts.push_back({0, cursorX});

    const char* src = text;
    int charIdx = 0;
    Uint32 prevGlyphIndex = 0xFFFFFFFF;
    int prevFontHandle    = -1;

    while (*src) {
        // Scan ahead to find the end of the current word (up to next whitespace or end).
        // Only do this when wrap is enabled.
        if (params.wrapWidth > 0.0f) {
            float wordW = 0.0f;
            const char* ws = src;
            Uint32 prevWIdx = prevGlyphIndex;
            bool inWord = false;
            while (*ws) {
                unsigned char b = (unsigned char)*ws;
                if (b == ' ' || b == '\t' || b == '\n') {
                    if (inWord) break;      // found end of word
                    ws++;                   // skip leading whitespace
                    prevWIdx = 0xFFFFFFFF;
                    continue;
                }
                inWord = true;
                const char* tmp = ws;
                Uint32 cp = decodeUtf8(&tmp);
                const FontGlyphEntry* ge = fontManager_->lookupGlyph(fontHandle, cp);
                if (ge) {
                    Sint32 kern = 0;
                    if (prevWIdx != 0xFFFFFFFF)
                        kern = fontManager_->getKern(fontHandle, prevWIdx, ge->glyphIndex);
                    wordW += ((float)ge->advanceWidth + (float)kern) * scale;
                    prevWIdx = ge->glyphIndex;
                }
                ws = tmp;
            }
            // If word would overflow current line, wrap first.
            float lineWidth = cursorX - lineStarts.back().startX;
            if (lineWidth > 0.0f && lineWidth + wordW > params.wrapWidth) {
                cursorX = params.originX;
                cursorY -= lineHeight;
                lineStarts.push_back({(int)glyphs_.size(), cursorX});
                prevGlyphIndex = 0xFFFFFFFF;
                prevFontHandle = -1;
            }
        }

        Uint32 cp = decodeUtf8(&src);

        if (cp == '\n') {
            cursorX = params.originX;
            cursorY -= lineHeight;
            lineStarts.push_back({(int)glyphs_.size(), cursorX});
            prevGlyphIndex = 0xFFFFFFFF;
            prevFontHandle = -1;
            charIdx++;
            continue;
        }

        if (cp == '\t') {
            if (tabStop > 0.0f) {
                float col = cursorX - params.originX;
                float next = (SDL_floorf(col / tabStop) + 1.0f) * tabStop;
                cursorX = params.originX + next;
            }
            prevGlyphIndex = 0xFFFFFFFF;
            charIdx++;
            continue;
        }

        const FontGlyphEntry* ge = fontManager_->lookupGlyph(fontHandle, cp);
        if (!ge) {
            // Try the replacement character
            ge = fontManager_->lookupGlyph(fontHandle, 0xFFFD);
        }
        if (!ge) {
            charIdx++;
            continue;
        }

        // Apply kerning with previous glyph in same font
        if (prevGlyphIndex != 0xFFFFFFFF && prevFontHandle == fontHandle) {
            Sint32 kern = fontManager_->getKern(fontHandle, prevGlyphIndex, ge->glyphIndex);
            cursorX += (float)kern * scale;
        }

        GlyphInstance gi{};
        gi.codepoint   = cp;
        gi.glyphIndex  = ge->glyphIndex;
        gi.fontHandle  = fontHandle;
        gi.x           = cursorX;
        gi.y           = cursorY;
        gi.scale       = scale;
        gi.charIndex   = charIdx;
        gi.hasOutline  = (ge->sdfSize > 0);

        glyphs_.push_back(gi);

        cursorX += (float)ge->advanceWidth * scale;
        prevGlyphIndex = ge->glyphIndex;
        prevFontHandle = fontHandle;
        charIdx++;
    }

    textByteLen_ = (int)(src - text);

    // --- Alignment pass ---
    if (params.alignment != TEXT_ALIGN_LEFT && lineStarts.size() > 0) {
        // Determine end index for each line
        int numLines = (int)lineStarts.size();
        for (int li = 0; li < numLines; li++) {
            int startGlyph = lineStarts[li].glyphIdx;
            int endGlyph   = (li + 1 < numLines) ? lineStarts[li + 1].glyphIdx : (int)glyphs_.size();
            if (endGlyph <= startGlyph) continue;

            float lineEndX = glyphs_[endGlyph - 1].x;
            // Add the last glyph's advance width
            const FontGlyphEntry* lastGe =
                fontManager_->lookupGlyph(glyphs_[endGlyph - 1].fontHandle,
                                          glyphs_[endGlyph - 1].codepoint);
            if (lastGe) lineEndX += (float)lastGe->advanceWidth * glyphs_[endGlyph - 1].scale;

            float lineWidth = lineEndX - lineStarts[li].startX;
            float shift = 0.0f;
            if (params.alignment == TEXT_ALIGN_CENTER) {
                shift = -lineWidth * 0.5f;
            } else if (params.alignment == TEXT_ALIGN_RIGHT) {
                shift = -lineWidth;
            }

            for (int gi = startGlyph; gi < endGlyph; gi++) {
                glyphs_[gi].x += shift;
            }
        }
    }
}
