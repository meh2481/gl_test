#include "TextLayout.h"
#include "TextLayer.h"
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

    // Helper: resolve effective font handle for a character index.
    // Font-override spans use MARKUP_EFFECT_FONT (effect id 6) in the span array.
    auto getEffectiveFont = [&](int charIdx) -> int {
        if (params.fontSpans && params.numFontSpans > 0) {
            for (int si = 0; si < params.numFontSpans; si++) {
                const MarkupSpan& fs = params.fontSpans[si];
                if (charIdx >= fs.startChar && charIdx < fs.endChar) {
                    return fs.fontHandle;
                }
            }
        }
        return fontHandle;
    };

    // Each glyph placed on the current line; line starts are tracked for alignment.
    struct LineStart { int glyphIdx; float startX; };
    Vector<LineStart> lineStarts(*allocator_, "TextLayout::lineStarts");
    lineStarts.push_back({0, cursorX});

    const char* src = text;
    int charIdx = 0;
    Uint32 prevGlyphIndex = 0xFFFFFFFF;
    int prevFontHandle    = -1;
    bool atSoftWrapLineStart = false;

    while (*src) {
        // Scan ahead to find the end of the current word (up to next whitespace or end).
        // Only do this when wrap is enabled.
        if (params.wrapWidth > 0.0f) {
            float wordW = 0.0f;
            const char* ws = src;
            Uint32 prevWIdx = prevGlyphIndex;
            int prevWFontHandle = prevFontHandle;
            int    wcharIdx = charIdx;
            bool inWord = false;
            while (*ws) {
                unsigned char b = (unsigned char)*ws;
                if (b == ' ' || b == '\t' || b == '\n') {
                    if (inWord) break;      // found end of word
                    ws++;                   // skip leading whitespace
                    wcharIdx++;
                    prevWIdx = 0xFFFFFFFF;
                    prevWFontHandle = -1;
                    continue;
                }
                inWord = true;
                const char* tmp = ws;
                Uint32 cp = decodeUtf8(&tmp);
                int ef = getEffectiveFont(wcharIdx);
                Sint32 efUnitsPerEM = fontManager_->getUnitsPerEM(ef);
                if (efUnitsPerEM <= 0) efUnitsPerEM = unitsPerEM;
                float efScale = params.pointSize / (float)efUnitsPerEM;
                const FontGlyphEntry* ge = fontManager_->lookupGlyph(ef, cp);
                if (!ge) ge = fontManager_->lookupGlyph(ef, 0xFFFD);
                if (!ge) ge = fontManager_->lookupGlyphByIndex(ef, 0);  // Fallback to box glyph
                if (ge) {
                    Sint32 kern = 0;
                    if (prevWIdx != 0xFFFFFFFF && prevWFontHandle == ef)
                        kern = fontManager_->getKern(ef, prevWIdx, ge->glyphIndex);
                    wordW += ((float)ge->advanceWidth + (float)kern) * efScale;
                    prevWIdx = ge->glyphIndex;
                    prevWFontHandle = ef;
                }
                ws = tmp;
                wcharIdx++;
            }
            // If word would overflow current line, wrap first.
            float lineWidth = cursorX - lineStarts.back().startX;
            if (lineWidth > 0.0f && lineWidth + wordW > params.wrapWidth) {
                cursorX = params.originX;
                cursorY -= lineHeight;
                lineStarts.push_back({(int)glyphs_.size(), cursorX});
                prevGlyphIndex = 0xFFFFFFFF;
                prevFontHandle = -1;
                atSoftWrapLineStart = true;
            }
        }

        Uint32 cp = decodeUtf8(&src);

        if (cp == '\n') {
            cursorX = params.originX;
            cursorY -= lineHeight;
            lineStarts.push_back({(int)glyphs_.size(), cursorX});
            prevGlyphIndex = 0xFFFFFFFF;
            prevFontHandle = -1;
            atSoftWrapLineStart = false;
            charIdx++;
            continue;
        }

        // Drop leading spaces/tabs only for auto-wrapped lines.
        if (atSoftWrapLineStart && (cp == ' ' || cp == '\t')) {
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
            atSoftWrapLineStart = false;
            charIdx++;
            continue;
        }

        // Resolve effective font for this character
        int ef = getEffectiveFont(charIdx);
        Sint32 efUnitsPerEM = fontManager_->getUnitsPerEM(ef);
        if (efUnitsPerEM <= 0) efUnitsPerEM = unitsPerEM;
        float efScale = params.pointSize / (float)efUnitsPerEM;

        const FontGlyphEntry* ge = fontManager_->lookupGlyph(ef, cp);
        if (!ge) {
            // Try the replacement character
            ge = fontManager_->lookupGlyph(ef, 0xFFFD);
        }
        if (!ge) {
            // Fallback to the box glyph (glyph index 0) for unavailable glyphs
            ge = fontManager_->lookupGlyphByIndex(ef, 0);
        }
        if (!ge) {
            charIdx++;
            continue;
        }

        // Apply kerning with previous glyph in same font
        if (prevGlyphIndex != 0xFFFFFFFF && prevFontHandle == ef) {
            Sint32 kern = fontManager_->getKern(ef, prevGlyphIndex, ge->glyphIndex);
            cursorX += (float)kern * efScale;
        }

        GlyphInstance gi{};
        gi.codepoint   = cp;
        gi.glyphIndex  = ge->glyphIndex;
        gi.fontHandle  = ef;
        gi.x           = cursorX;
        gi.y           = cursorY;
        gi.scale       = efScale;
        gi.charIndex   = charIdx;
        gi.hasOutline  = (ge->sdfSize > 0);

        glyphs_.push_back(gi);

        cursorX += (float)ge->advanceWidth * efScale;
        prevGlyphIndex = ge->glyphIndex;
        prevFontHandle = ef;
        atSoftWrapLineStart = false;
        charIdx++;
    }

    textByteLen_ = (int)(src - text);

    // --- Alignment pass ---
    if (params.alignment != TEXT_ALIGN_LEFT && lineStarts.size() > 0) {
        int numLines = (int)lineStarts.size();
        Vector<float> lineWidths(*allocator_, "TextLayout::lineWidths");
        lineWidths.resize((Uint64)numLines, 0.0f);

        float maxLineWidth = 0.0f;
        for (int li = 0; li < numLines; li++) {
            int startGlyph = lineStarts[li].glyphIdx;
            int endGlyph   = (li + 1 < numLines) ? lineStarts[li + 1].glyphIdx : (int)glyphs_.size();
            if (endGlyph <= startGlyph) continue;

            float lineEndX = glyphs_[endGlyph - 1].x;
            const FontGlyphEntry* lastGe =
                fontManager_->lookupGlyph(glyphs_[endGlyph - 1].fontHandle,
                                          glyphs_[endGlyph - 1].codepoint);
            if (lastGe) lineEndX += (float)lastGe->advanceWidth * glyphs_[endGlyph - 1].scale;

            float lineWidth = lineEndX - lineStarts[li].startX;
            if (lineWidth < 0.0f) lineWidth = 0.0f;
            lineWidths[li] = lineWidth;
            if (lineWidth > maxLineWidth) maxLineWidth = lineWidth;
        }

        float alignWidth = params.wrapWidth > 0.0f ? params.wrapWidth : maxLineWidth;

        for (int li = 0; li < numLines; li++) {
            int startGlyph = lineStarts[li].glyphIdx;
            int endGlyph   = (li + 1 < numLines) ? lineStarts[li + 1].glyphIdx : (int)glyphs_.size();
            if (endGlyph <= startGlyph) continue;

            float lineWidth = lineWidths[li];
            float shift = 0.0f;
            if (params.alignment == TEXT_ALIGN_CENTER) {
                shift = (alignWidth - lineWidth) * 0.5f;
            } else if (params.alignment == TEXT_ALIGN_RIGHT) {
                shift = (alignWidth - lineWidth);
            }

            for (int gi = startGlyph; gi < endGlyph; gi++) {
                glyphs_[gi].x += shift;
            }
        }
    }
}
