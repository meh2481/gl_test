#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

// ResourceTypes gives us FontBinaryHeader, FontGlyphEntryDisk, FontKernPairDisk,
// CompactShapeHeader, CompactContourHeader, and the FONT_* constants.
// SDL_stdinc.h (pulled in by ResourceTypes.h) provides Uint32, Sint32, etc.
#include "../src/core/ResourceTypes.h"

// ---------------------------------------------------------------------------
// Compact SDF glyph builder -- builds a CompactShapeHeader + CompactContourHeader[]
// + variable-length segment stream from a FreeType outline, normalised by unitsPerEM.
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

// Encode a normalised float coordinate as a Sint16 using FONT_COORD_SCALE.
static Sint16 encodeCoord(float v) {
    float scaled = v * FONT_COORD_SCALE;
    if (scaled >  32767.0f) scaled =  32767.0f;
    if (scaled < -32768.0f) scaled = -32768.0f;
    return static_cast<Sint16>(static_cast<int>(roundf(scaled)));
}

// Returns true if the segment is a degenerate cubic (i.e. a straight line).
// Straight lines are stored by lineToCallback with p1==p0 and p2==p3.
static bool segIsLine(const BinarySegment& s) {
    float d1x = s.p1x - s.p0x, d1y = s.p1y - s.p0y;
    float d2x = s.p2x - s.p3x, d2y = s.p2y - s.p3y;
    return (d1x*d1x + d1y*d1y < 1e-12f) && (d2x*d2x + d2y*d2y < 1e-12f);
}

// Write a 16-bit little-endian value into a byte vector.
static void writeS16(std::vector<uint8_t>& buf, Sint16 v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
static void writeU16(std::vector<uint8_t>& buf, Uint16 v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

// Build the compact SDF blob for the currently loaded glyph slot.
// normScale should be face->units_per_EM (so all glyphs use consistent normalisation).
// On success, output contains CompactShapeHeader + CompactContourHeader[] + segment stream.
// An empty glyph (no outline) returns an output with numContours==0 and totalSegments==0.
static bool buildCompactSdfBlob(FT_Face face, float normScale,
                                 std::vector<uint8_t>& output) {
    FT_GlyphSlot slot    = face->glyph;
    FT_Outline&  outline = slot->outline;

    if (outline.n_contours == 0 || outline.n_points == 0) {
        // Empty glyph — write a zero CompactShapeHeader so caller can detect it.
        output.resize(sizeof(CompactShapeHeader), 0);
        return true;
    }

    BinaryGlyphBuilder builder;
    builder.normScale = normScale;
    FT_Outline_Funcs funcs = builder.funcs();

    // Process each contour separately so we can inject a closing segment if needed.
    int ptStart = 0;
    for (int c = 0; c < outline.n_contours; c++) {
        int ptEnd = outline.contours[c];
        int nPts  = ptEnd - ptStart + 1;

        FT_Outline sub;
        sub.n_points   = static_cast<short>(nPts);
        sub.n_contours = 1;
        sub.points     = outline.points + ptStart;
        sub.tags       = outline.tags   + ptStart;
        using ContourIndex = std::remove_pointer<decltype(outline.contours)>::type;
        ContourIndex relEnd = static_cast<ContourIndex>(nPts - 1);
        sub.contours   = &relEnd;
        sub.flags      = outline.flags;

        FT_Error err = FT_Outline_Decompose(&sub, &funcs, &builder);
        assert(err == 0);

        // Close the contour if the last point does not coincide with the first.
        if (!builder.contours.empty() && !builder.contours.back().segs.empty()) {
            const BinarySegment& last  = builder.contours.back().segs.back();
            const BinarySegment& first = builder.contours.back().segs.front();
            float dx = last.p3x - first.p0x;
            float dy = last.p3y - first.p0y;
            if (dx * dx + dy * dy > 1e-10f) {
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

    // Remove empty contours.
    builder.contours.erase(
        std::remove_if(builder.contours.begin(), builder.contours.end(),
                       [](const BinaryContour& c){ return c.segs.empty(); }),
        builder.contours.end());

    if (builder.contours.empty() ||
        builder.ctrlMaxX <= builder.ctrlMinX ||
        builder.ctrlMaxY <= builder.ctrlMinY) {
        output.resize(sizeof(CompactShapeHeader), 0);
        return true;
    }

    // Count total segments.
    Uint32 totalSegs = 0;
    for (const BinaryContour& bc : builder.contours)
        totalSegs += static_cast<Uint32>(bc.segs.size());

    assert(builder.contours.size() <= 65535u);
    assert(totalSegs <= 65535u);

    // --- Build output buffer ---
    // 1. CompactShapeHeader placeholder (filled in after encoding).
    output.clear();
    output.resize(sizeof(CompactShapeHeader));

    // 2. CompactContourHeader[numContours].
    for (const BinaryContour& bc : builder.contours) {
        assert(bc.segs.size() <= 65535u);
        writeU16(output, static_cast<Uint16>(bc.segs.size()));
    }

    // 3. Segment stream: for each contour, write start point then typed segments.
    for (const BinaryContour& bc : builder.contours) {
        assert(!bc.segs.empty());
        // Explicit start point p0 of the first segment.
        writeS16(output, encodeCoord(bc.segs[0].p0x));
        writeS16(output, encodeCoord(bc.segs[0].p0y));
        // Segments.
        for (const BinarySegment& s : bc.segs) {
            if (segIsLine(s)) {
                output.push_back(static_cast<uint8_t>(FONT_SEG_LINE));
                writeS16(output, encodeCoord(s.p3x));
                writeS16(output, encodeCoord(s.p3y));
            } else {
                output.push_back(static_cast<uint8_t>(FONT_SEG_CUBIC));
                writeS16(output, encodeCoord(s.p1x));
                writeS16(output, encodeCoord(s.p1y));
                writeS16(output, encodeCoord(s.p2x));
                writeS16(output, encodeCoord(s.p2y));
                writeS16(output, encodeCoord(s.p3x));
                writeS16(output, encodeCoord(s.p3y));
            }
        }
    }

    // Fill in CompactShapeHeader.
    static const float BBOX_MARGIN = 0.02f;
    CompactShapeHeader csh{};
    csh.numContours   = static_cast<Uint16>(builder.contours.size());
    csh.totalSegments = static_cast<Uint16>(totalSegs);
    csh.bboxMinX      = encodeCoord(builder.ctrlMinX - BBOX_MARGIN);
    csh.bboxMinY      = encodeCoord(builder.ctrlMinY - BBOX_MARGIN);
    csh.bboxMaxX      = encodeCoord(builder.ctrlMaxX + BBOX_MARGIN);
    csh.bboxMaxY      = encodeCoord(builder.ctrlMaxY + BBOX_MARGIN);
    memcpy(output.data(), &csh, sizeof(csh));

    return true;
}

// Write a binary .font file alongside the per-glyph SVG directory.
// outPath is the path for the .font file (e.g. "res/fonts/aileron/Aileron-Regular.font").
static bool writeFontBinary(FT_Face face,
                             const std::vector<FT_ULong>& indexToCodepoint,
                             const std::vector<bool>& hasCp,
                             const std::filesystem::path& outPath) {
    const float normScale = static_cast<float>(face->units_per_EM);

    // --- Build one entry per glyph index in order (0..face->num_glyphs-1) ---
    // Storing in glyph-index order means array position == font-internal glyph index,
    // so the on-disk FontGlyphEntryDisk does not need to store the index explicitly.
    std::vector<FontGlyphEntryDisk>    glyphEntries;
    std::vector<std::vector<uint8_t>>  sdfBlobs;

    glyphEntries.reserve(static_cast<size_t>(face->num_glyphs));
    sdfBlobs.reserve(static_cast<size_t>(face->num_glyphs));

    for (FT_Long gi = 0; gi < face->num_glyphs; gi++) {
        FT_UInt gidx = static_cast<FT_UInt>(gi);

        FontGlyphEntryDisk entry{};
        entry.codepoint = hasCp[gidx] ? static_cast<Uint32>(indexToCodepoint[gidx]) : 0u;

        std::vector<uint8_t> blob;

        FT_Error err = FT_Load_Glyph(face, gidx, FT_LOAD_NO_BITMAP | FT_LOAD_NO_SCALE);
        if (err == 0 && face->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
            if (!buildCompactSdfBlob(face, normScale, blob)) {
                // Internal error — treat as empty glyph.
                blob.clear();
                blob.resize(sizeof(CompactShapeHeader), 0);
            }
            // Detect non-empty SDF blob: check numContours in CompactShapeHeader.
            assert(blob.size() >= sizeof(CompactShapeHeader));
            const CompactShapeHeader* csh =
                reinterpret_cast<const CompactShapeHeader*>(blob.data());
            if (csh->numContours > 0) {
                entry.sdfSize = static_cast<Uint32>(blob.size());
            }
            entry.advanceWidth =
                static_cast<Sint16>(face->glyph->metrics.horiAdvance);
            entry.leftBearing  =
                static_cast<Sint16>(face->glyph->metrics.horiBearingX);
        } else {
            // Glyph index not loadable or not outline — write an empty placeholder.
            blob.resize(sizeof(CompactShapeHeader), 0);
        }

        // sdfOffset filled in below.
        glyphEntries.push_back(entry);
        sdfBlobs.push_back(std::move(blob));
    }

    // Compute compact SDF offsets (relative to start of SDF section).
    // Only glyphs with sdfSize > 0 get a blob written to the SDF section.
    Uint32 sdfOffset = 0;
    for (size_t i = 0; i < glyphEntries.size(); i++) {
        if (glyphEntries[i].sdfSize > 0) {
            glyphEntries[i].sdfOffset = sdfOffset;
            sdfOffset += glyphEntries[i].sdfSize;
        }
    }

    // --- Collect kerning pairs ---
    std::vector<FontKernPairDisk> kernPairs;
    if (FT_HAS_KERNING(face)) {
        for (FT_Long li = 0; li < face->num_glyphs; li++) {
            for (FT_Long ri = 0; ri < face->num_glyphs; ri++) {
                FT_Vector kern;
                FT_Error kerr = FT_Get_Kerning(face,
                                               static_cast<FT_UInt>(li),
                                               static_cast<FT_UInt>(ri),
                                               FT_KERNING_UNSCALED, &kern);
                if (kerr == 0 && kern.x != 0) {
                    FontKernPairDisk kp{};
                    assert(li <= 65535 && ri <= 65535);
                    kp.leftGlyphIndex  = static_cast<Uint16>(li);
                    kp.rightGlyphIndex = static_cast<Uint16>(ri);
                    kp.kernValue       = static_cast<Sint32>(kern.x);
                    kernPairs.push_back(kp);
                }
            }
        }
        std::sort(kernPairs.begin(), kernPairs.end(),
                  [](const FontKernPairDisk& a, const FontKernPairDisk& b) {
                      if (a.leftGlyphIndex != b.leftGlyphIndex)
                          return a.leftGlyphIndex < b.leftGlyphIndex;
                      return a.rightGlyphIndex < b.rightGlyphIndex;
                  });
    }

    // --- Write binary file ---
    FontBinaryHeader hdr{};
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
                  static_cast<std::streamsize>(glyphEntries.size() * sizeof(FontGlyphEntryDisk)));
    if (!kernPairs.empty())
        out.write(reinterpret_cast<const char*>(kernPairs.data()),
                  static_cast<std::streamsize>(kernPairs.size() * sizeof(FontKernPairDisk)));

    // Write compact SDF blobs in glyph-index order; skip empty glyphs.
    for (size_t i = 0; i < sdfBlobs.size(); i++) {
        if (glyphEntries[i].sdfSize > 0) {
            out.write(reinterpret_cast<const char*>(sdfBlobs[i].data()),
                      static_cast<std::streamsize>(sdfBlobs[i].size()));
        }
    }

    size_t lineSegs = 0, cubicSegs = 0;
    for (size_t i = 0; i < sdfBlobs.size(); i++) {
        if (glyphEntries[i].sdfSize > 0) {
            const CompactShapeHeader* csh =
                reinterpret_cast<const CompactShapeHeader*>(sdfBlobs[i].data());
            Uint32 nc = csh->numContours;
            const uint8_t* p = sdfBlobs[i].data()
                             + sizeof(CompactShapeHeader)
                             + nc * sizeof(CompactContourHeader);
            const CompactContourHeader* cchs =
                reinterpret_cast<const CompactContourHeader*>(
                    sdfBlobs[i].data() + sizeof(CompactShapeHeader));
            for (Uint32 ci = 0; ci < nc; ci++) {
                p += 4;  // skip start point Sint16[2]
                for (Uint16 si = 0; si < cchs[ci].numSegments; si++) {
                    Uint8 type = *p++;
                    if (type == FONT_SEG_LINE) {
                        p += 4;  // Sint16[2]
                        lineSegs++;
                    } else {
                        p += 12; // Sint16[6]
                        cubicSegs++;
                    }
                }
            }
        }
    }

    printf("Font: %zu glyphs, %zu kern pairs, %zu line + %zu cubic segs -> %s\n",
           glyphEntries.size(), kernPairs.size(), lineSegs, cubicSegs,
           outPath.string().c_str());
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
