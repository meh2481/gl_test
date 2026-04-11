#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

// ResourceTypes gives us FontBinaryHeader, FontGlyphEntry, FontKernPair,
// SdfShapeHeader, SdfContourHeader, and SdfSegment.
// SDL_stdinc.h (pulled in by ResourceTypes.h) provides Uint32, Sint32, etc.
#include "../src/core/ResourceTypes.h"

// ---------------------------------------------------------------------------
// Binary SDF glyph builder -- builds SdfShapeHeader+SdfContourHeader[]+SdfSegment[]
// from a FreeType outline, normalised by the font's unitsPerEM.
// ---------------------------------------------------------------------------

struct BinarySegment { float p0x,p0y, p1x,p1y, p2x,p2y, p3x,p3y; };
struct BinaryContour { std::vector<BinarySegment> segs; };

struct BinaryGlyphBuilder {
    std::vector<BinaryContour> contours;
    float currX = 0.0f, currY = 0.0f;
    float normScale = 1.0f;  // set to (float)unitsPerEM before use

    // Bounding box over all control points (normalised coords).
    float ctrlMinX =  1e30f, ctrlMinY =  1e30f;
    float ctrlMaxX = -1e30f, ctrlMaxY = -1e30f;

    float nx(float v) const { return v / normScale; }
    float ny(float v) const { return v / normScale; }  // FT is already Y-up, no flip

    void updateBbox(float x, float y) {
        if (x < ctrlMinX) ctrlMinX = x;
        if (x > ctrlMaxX) ctrlMaxX = x;
        if (y < ctrlMinY) ctrlMinY = y;
        if (y > ctrlMaxY) ctrlMaxY = y;
    }

    void pushSeg(float x0, float y0,
                 float x1, float y1,
                 float x2, float y2,
                 float x3, float y3) {
        assert(!contours.empty());
        BinarySegment s;
        s.p0x = nx(x0); s.p0y = ny(y0);
        s.p1x = nx(x1); s.p1y = ny(y1);
        s.p2x = nx(x2); s.p2y = ny(y2);
        s.p3x = nx(x3); s.p3y = ny(y3);
        updateBbox(s.p0x, s.p0y);
        updateBbox(s.p1x, s.p1y);
        updateBbox(s.p2x, s.p2y);
        updateBbox(s.p3x, s.p3y);
        contours.back().segs.push_back(s);
    }

    static int moveToCallback(const FT_Vector* to, void* user) {
        BinaryGlyphBuilder* b = static_cast<BinaryGlyphBuilder*>(user);
        b->contours.push_back(BinaryContour{});
        b->currX = static_cast<float>(to->x);
        b->currY = static_cast<float>(to->y);
        return 0;
    }

    static int lineToCallback(const FT_Vector* to, void* user) {
        BinaryGlyphBuilder* b = static_cast<BinaryGlyphBuilder*>(user);
        float x0 = b->currX, y0 = b->currY;
        float x3 = static_cast<float>(to->x), y3 = static_cast<float>(to->y);
        // Degenerate cubic (line): C1 = P0, C2 = P3
        b->pushSeg(x0, y0, x0, y0, x3, y3, x3, y3);
        b->currX = x3; b->currY = y3;
        return 0;
    }

    // Conic (quadratic) bezier: convert to cubic.
    // Q→C: C1 = P0 + 2/3*(Q-P0), C2 = P3 + 2/3*(Q-P3)
    static int conicToCallback(const FT_Vector* control, const FT_Vector* to, void* user) {
        BinaryGlyphBuilder* b = static_cast<BinaryGlyphBuilder*>(user);
        float x0 = b->currX, y0 = b->currY;
        float qx = static_cast<float>(control->x), qy = static_cast<float>(control->y);
        float x3 = static_cast<float>(to->x), y3 = static_cast<float>(to->y);
        float x1 = x0 + (2.0f / 3.0f) * (qx - x0);
        float y1 = y0 + (2.0f / 3.0f) * (qy - y0);
        float x2 = x3 + (2.0f / 3.0f) * (qx - x3);
        float y2 = y3 + (2.0f / 3.0f) * (qy - y3);
        b->pushSeg(x0, y0, x1, y1, x2, y2, x3, y3);
        b->currX = x3; b->currY = y3;
        return 0;
    }

    static int cubicToCallback(const FT_Vector* c1, const FT_Vector* c2,
                               const FT_Vector* to, void* user) {
        BinaryGlyphBuilder* b = static_cast<BinaryGlyphBuilder*>(user);
        b->pushSeg(b->currX, b->currY,
                   static_cast<float>(c1->x), static_cast<float>(c1->y),
                   static_cast<float>(c2->x), static_cast<float>(c2->y),
                   static_cast<float>(to->x), static_cast<float>(to->y));
        b->currX = static_cast<float>(to->x);
        b->currY = static_cast<float>(to->y);
        return 0;
    }

    FT_Outline_Funcs funcs() const {
        FT_Outline_Funcs f;
        f.move_to  = moveToCallback;
        f.line_to  = lineToCallback;
        f.conic_to = conicToCallback;
        f.cubic_to = cubicToCallback;
        f.shift    = 0;
        f.delta    = 0;
        return f;
    }
};

// Build the SDF binary blob for the currently loaded glyph slot.
// normScale should be face->units_per_EM (so all glyphs use consistent normalisation).
// Returns false only on internal error; an empty glyph (no outline) returns true
// with an empty SdfShapeHeader blob.
static bool buildGlyphSdfBlob(FT_Face face, float normScale,
                               std::vector<char>& output) {
    FT_GlyphSlot slot   = face->glyph;
    FT_Outline&  outline = slot->outline;

    if (outline.n_contours == 0 || outline.n_points == 0) {
        SdfShapeHeader emptyHdr{};
        output.resize(sizeof(SdfShapeHeader));
        memcpy(output.data(), &emptyHdr, sizeof(emptyHdr));
        return true;
    }

    BinaryGlyphBuilder builder;
    builder.normScale = normScale;
    FT_Outline_Funcs funcs = builder.funcs();

    // Process each contour separately so we can inject a Z close.
    int ptStart = 0;
    for (int c = 0; c < outline.n_contours; c++) {
        int ptEnd = outline.contours[c];
        int nPts  = ptEnd - ptStart + 1;

        FT_Outline sub;
        sub.n_points   = static_cast<short>(nPts);
        sub.n_contours = 1;
        sub.points     = outline.points + ptStart;
        sub.tags       = outline.tags   + ptStart;
        FT_UShort relEnd = static_cast<FT_UShort>(nPts - 1);
        sub.contours   = &relEnd;
        sub.flags      = outline.flags;

        FT_Error err = FT_Outline_Decompose(&sub, &funcs, &builder);
        assert(err == 0);

        // Close the contour if the last point does not coincide with the first.
        if (!builder.contours.empty() && !builder.contours.back().segs.empty()) {
            const BinarySegment& last = builder.contours.back().segs.back();
            const BinarySegment& first = builder.contours.back().segs.front();
            float dx = last.p3x - first.p0x;
            float dy = last.p3y - first.p0y;
            if (dx * dx + dy * dy > 1e-10f) {
                // Add a degenerate cubic closing segment
                BinarySegment cs;
                cs.p0x = last.p3x;  cs.p0y = last.p3y;
                cs.p1x = last.p3x;  cs.p1y = last.p3y;
                cs.p2x = first.p0x; cs.p2y = first.p0y;
                cs.p3x = first.p0x; cs.p3y = first.p0y;
                builder.updateBbox(cs.p0x, cs.p0y);
                builder.updateBbox(cs.p3x, cs.p3y);
                builder.contours.back().segs.push_back(cs);
            }
        }

        ptStart = ptEnd + 1;
    }

    // Remove empty contours
    builder.contours.erase(
        std::remove_if(builder.contours.begin(), builder.contours.end(),
                       [](const BinaryContour& c){ return c.segs.empty(); }),
        builder.contours.end());

    if (builder.contours.empty() ||
        builder.ctrlMaxX <= builder.ctrlMinX ||
        builder.ctrlMaxY <= builder.ctrlMinY) {
        SdfShapeHeader emptyHdr{};
        output.resize(sizeof(SdfShapeHeader));
        memcpy(output.data(), &emptyHdr, sizeof(emptyHdr));
        return true;
    }

    // Build flat arrays
    std::vector<SdfContourHeader> contourHdrs;
    std::vector<SdfSegment>       flatSegs;
    for (const BinaryContour& c : builder.contours) {
        SdfContourHeader hdr{};
        hdr.numSegments   = static_cast<Uint32>(c.segs.size());
        hdr.winding       = 1;  // not used by shader; set +1 as placeholder
        hdr.segmentOffset = static_cast<Uint32>(flatSegs.size());
        hdr.pad           = 0;
        contourHdrs.push_back(hdr);
        for (const BinarySegment& s : c.segs) {
            SdfSegment seg;
            seg.p0x = s.p0x; seg.p0y = s.p0y;
            seg.p1x = s.p1x; seg.p1y = s.p1y;
            seg.p2x = s.p2x; seg.p2y = s.p2y;
            seg.p3x = s.p3x; seg.p3y = s.p3y;
            flatSegs.push_back(seg);
        }
    }

    static const float BBOX_MARGIN = 0.02f;
    SdfShapeHeader shapeHdr{};
    shapeHdr.numContours   = static_cast<Uint32>(contourHdrs.size());
    shapeHdr.bboxMinX      = builder.ctrlMinX - BBOX_MARGIN;
    shapeHdr.bboxMinY      = builder.ctrlMinY - BBOX_MARGIN;
    shapeHdr.bboxMaxX      = builder.ctrlMaxX + BBOX_MARGIN;
    shapeHdr.bboxMaxY      = builder.ctrlMaxY + BBOX_MARGIN;
    shapeHdr.totalSegments = static_cast<Uint32>(flatSegs.size());

    output.resize(sizeof(SdfShapeHeader)
                + contourHdrs.size() * sizeof(SdfContourHeader)
                + flatSegs.size()    * sizeof(SdfSegment));
    char* dst = output.data();
    memcpy(dst, &shapeHdr, sizeof(shapeHdr));
    dst += sizeof(shapeHdr);
    memcpy(dst, contourHdrs.data(), contourHdrs.size() * sizeof(SdfContourHeader));
    dst += contourHdrs.size() * sizeof(SdfContourHeader);
    memcpy(dst, flatSegs.data(), flatSegs.size() * sizeof(SdfSegment));

    return true;
}

// Write a binary .font file alongside the per-glyph SVG directory.
// outPath is the path for the .font file (e.g. "res/fonts/aileron/Aileron-Regular.font").
static bool writeFontBinary(FT_Face face,
                             const std::vector<FT_ULong>& indexToCodepoint,
                             const std::vector<bool>& hasCp,
                             const std::filesystem::path& outPath) {
    const float normScale = static_cast<float>(face->units_per_EM);

    // --- Collect glyph entries and SDF blobs ---
    std::vector<FontGlyphEntry> glyphEntries;
    std::vector<std::vector<char>> sdfBlobs;  // parallel to glyphEntries

    for (FT_Long gi = 0; gi < face->num_glyphs; gi++) {
        FT_UInt gidx = static_cast<FT_UInt>(gi);

        FT_Error err = FT_Load_Glyph(face, gidx,
                                     FT_LOAD_NO_BITMAP | FT_LOAD_NO_SCALE);
        if (err != 0) continue;
        if (face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) continue;

        std::vector<char> blob;
        if (!buildGlyphSdfBlob(face, normScale, blob)) continue;

        FontGlyphEntry entry{};
        entry.codepoint   = hasCp[gidx] ? static_cast<Uint32>(indexToCodepoint[gidx]) : 0u;
        entry.glyphIndex  = static_cast<Uint32>(gidx);
        entry.advanceWidth = static_cast<Sint32>(face->glyph->metrics.horiAdvance);
        entry.leftBearing  = static_cast<Sint32>(face->glyph->metrics.horiBearingX);
        // sdfOffset / sdfSize filled in below
        entry.sdfOffset = 0;
        entry.sdfSize   = 0;

        const SdfShapeHeader* shdr =
            reinterpret_cast<const SdfShapeHeader*>(blob.data());
        if (shdr->numContours > 0) {
            entry.sdfSize = static_cast<Uint32>(blob.size());
        }

        glyphEntries.push_back(entry);
        sdfBlobs.push_back(std::move(blob));
    }

    // Sort glyph entries by codepoint (0 = uncoded glyphs come first)
    // Keep blobs parallel by sorting an index array then permuting.
    std::vector<size_t> order(glyphEntries.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return glyphEntries[a].codepoint < glyphEntries[b].codepoint;
    });

    std::vector<FontGlyphEntry>      sortedEntries(glyphEntries.size());
    std::vector<std::vector<char>>   sortedBlobs  (glyphEntries.size());
    for (size_t i = 0; i < order.size(); i++) {
        sortedEntries[i] = glyphEntries[order[i]];
        sortedBlobs[i]   = std::move(sdfBlobs[order[i]]);
    }
    glyphEntries = std::move(sortedEntries);
    sdfBlobs     = std::move(sortedBlobs);

    // Compute SDF offsets (relative to start of SDF section)
    Uint32 sdfOffset = 0;
    for (size_t i = 0; i < glyphEntries.size(); i++) {
        if (glyphEntries[i].sdfSize > 0) {
            glyphEntries[i].sdfOffset = sdfOffset;
            sdfOffset += glyphEntries[i].sdfSize;
        }
    }

    // --- Collect kerning pairs ---
    std::vector<FontKernPair> kernPairs;
    if (FT_HAS_KERNING(face)) {
        for (FT_Long li = 0; li < face->num_glyphs; li++) {
            for (FT_Long ri = 0; ri < face->num_glyphs; ri++) {
                FT_Vector kern;
                FT_Error kerr = FT_Get_Kerning(face,
                                               static_cast<FT_UInt>(li),
                                               static_cast<FT_UInt>(ri),
                                               FT_KERNING_UNSCALED, &kern);
                if (kerr == 0 && kern.x != 0) {
                    FontKernPair kp{};
                    kp.leftGlyphIndex  = static_cast<Uint32>(li);
                    kp.rightGlyphIndex = static_cast<Uint32>(ri);
                    kp.kernValue       = static_cast<Sint32>(kern.x);
                    kernPairs.push_back(kp);
                }
            }
        }
        std::sort(kernPairs.begin(), kernPairs.end(),
                  [](const FontKernPair& a, const FontKernPair& b) {
                      if (a.leftGlyphIndex != b.leftGlyphIndex)
                          return a.leftGlyphIndex < b.leftGlyphIndex;
                      return a.rightGlyphIndex < b.rightGlyphIndex;
                  });
    }

    // --- Write binary file ---
    FontBinaryHeader hdr{};
    hdr.magic        = FONT_BINARY_MAGIC;
    hdr.version      = FONT_BINARY_VERSION;
    hdr.numGlyphs    = static_cast<Uint32>(glyphEntries.size());
    hdr.numKernPairs = static_cast<Uint32>(kernPairs.size());
    hdr.unitsPerEM   = static_cast<Sint32>(face->units_per_EM);
    hdr.ascender     = static_cast<Sint32>(face->ascender);
    hdr.descender    = static_cast<Sint32>(face->descender);
    hdr.lineGap      = 0;  // FT doesn't expose line gap directly here

    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open()) {
        fprintf(stderr, "  ERROR: could not write %s\n", outPath.string().c_str());
        return false;
    }

    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    if (!glyphEntries.empty())
        out.write(reinterpret_cast<const char*>(glyphEntries.data()),
                  static_cast<std::streamsize>(glyphEntries.size() * sizeof(FontGlyphEntry)));
    if (!kernPairs.empty())
        out.write(reinterpret_cast<const char*>(kernPairs.data()),
                  static_cast<std::streamsize>(kernPairs.size() * sizeof(FontKernPair)));

    // Write SDF blobs (in codepoint-sorted order; skip empty glyphs)
    for (size_t i = 0; i < sdfBlobs.size(); i++) {
        if (glyphEntries[i].sdfSize > 0) {
            out.write(sdfBlobs[i].data(),
                      static_cast<std::streamsize>(sdfBlobs[i].size()));
        }
    }

    printf("  Binary font: %zu glyphs, %zu kern pairs -> %s\n",
           glyphEntries.size(), kernPairs.size(), outPath.string().c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Process one OTF file — output only the binary .font file
// ---------------------------------------------------------------------------

static void processFontFile(const std::filesystem::path& otfPath,
                              const std::string& familyName) {
    FT_Library ft;
    FT_Error err = FT_Init_FreeType(&ft);
    assert(err == 0);

    FT_Face face;
    err = FT_New_Face(ft, otfPath.string().c_str(), 0, &face);
    if (err != 0) {
        fprintf(stderr, "  [%s] ERROR: could not load face from %s (FT error %d)\n",
                familyName.c_str(), otfPath.filename().string().c_str(), err);
        FT_Done_FreeType(ft);
        return;
    }

    // Build a map from glyph index → Unicode codepoint using the best charmap.
    std::vector<FT_ULong> indexToCodepoint(static_cast<size_t>(face->num_glyphs), 0);
    std::vector<bool>     hasCp(static_cast<size_t>(face->num_glyphs), false);

    FT_UInt  glyphIndex = 0;
    FT_ULong cp = FT_Get_First_Char(face, &glyphIndex);
    while (glyphIndex != 0) {
        assert(glyphIndex < static_cast<FT_UInt>(face->num_glyphs));
        indexToCodepoint[glyphIndex] = cp;
        hasCp[glyphIndex]            = true;
        cp = FT_Get_Next_Char(face, cp, &glyphIndex);
    }

    // Write the binary .font file (same directory as the .otf, stem + ".font").
    std::filesystem::path fontBinaryPath =
        otfPath.parent_path() / (otfPath.stem().string() + ".font");
    writeFontBinary(face, indexToCodepoint, hasCp, fontBinaryPath);

    FT_Done_Face(face);
    FT_Done_FreeType(ft);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    std::filesystem::path fontsRoot = "res/fonts";

    if (argc == 2) {
        fontsRoot = argv[1];
    } else if (argc > 2) {
        fprintf(stderr, "Usage: font_extractor [fonts_root_dir]\n");
        fprintf(stderr, "  fonts_root_dir: path to directory containing font family subfolders\n");
        fprintf(stderr, "                  (default: res/fonts)\n");
        return 1;
    }

    if (!std::filesystem::is_directory(fontsRoot)) {
        fprintf(stderr, "ERROR: %s is not a directory\n", fontsRoot.string().c_str());
        return 1;
    }

    // Iterate font family subdirectories.
    for (const auto& familyEntry : std::filesystem::directory_iterator(fontsRoot)) {
        if (!familyEntry.is_directory()) {
            continue;
        }

        std::string familyName = familyEntry.path().filename().string();

        // Collect all .otf files in this family directory.
        std::vector<std::filesystem::path> otfFiles;
        for (const auto& fontEntry : std::filesystem::directory_iterator(familyEntry.path())) {
            if (!fontEntry.is_regular_file()) {
                continue;
            }
            std::string ext = fontEntry.path().extension().string();
            // Case-insensitive extension check.
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            if (ext == ".otf") {
                otfFiles.push_back(fontEntry.path());
            }
        }

        if (otfFiles.empty()) {
            continue;
        }

        // Sort for deterministic processing order.
        std::sort(otfFiles.begin(), otfFiles.end());

        for (const auto& otfPath : otfFiles) {
            processFontFile(otfPath, familyName);
        }
    }

    return 0;
}
