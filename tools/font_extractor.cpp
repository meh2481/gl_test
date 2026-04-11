#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

// ---------------------------------------------------------------------------
// SVG path builder -- accumulates path commands from FT_Outline_Decompose
// ---------------------------------------------------------------------------

struct SvgPathBuilder {
    std::ostringstream d;

    static int moveToCallback(const FT_Vector* to, void* user) {
        SvgPathBuilder* b = static_cast<SvgPathBuilder*>(user);
        b->d << "M " << to->x << "," << to->y << " ";
        return 0;
    }

    static int lineToCallback(const FT_Vector* to, void* user) {
        SvgPathBuilder* b = static_cast<SvgPathBuilder*>(user);
        b->d << "L " << to->x << "," << to->y << " ";
        return 0;
    }

    // Conic (quadratic) Bezier: the standard curve type in TrueType outlines.
    static int conicToCallback(const FT_Vector* control, const FT_Vector* to, void* user) {
        SvgPathBuilder* b = static_cast<SvgPathBuilder*>(user);
        b->d << "Q " << control->x << "," << control->y
             << " " << to->x << "," << to->y << " ";
        return 0;
    }

    // Cubic Bezier: used by CFF/PostScript outlines.
    static int cubicToCallback(const FT_Vector* control1, const FT_Vector* control2,
                               const FT_Vector* to, void* user) {
        SvgPathBuilder* b = static_cast<SvgPathBuilder*>(user);
        b->d << "C " << control1->x << "," << control1->y
             << " " << control2->x << "," << control2->y
             << " " << to->x << "," << to->y << " ";
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

// ---------------------------------------------------------------------------
// Glyph index → SVG output filename
// ---------------------------------------------------------------------------

static std::string glyphFilename(FT_ULong codepoint, FT_UInt glyphIndex, bool hasCp) {
    if (!hasCp) {
        // No charmap mapping — name by glyph index.
        char buf[32];
        snprintf(buf, sizeof(buf), "glyph%04u.svg", static_cast<unsigned>(glyphIndex));
        return std::string(buf);
    }
    // Printable ASCII (0x20–0x7E): use the character itself, escaping FS-unsafe ones.
    if (codepoint >= 0x20 && codepoint <= 0x7E) {
        // Characters that are invalid in filenames on common filesystems.
        const char unsafe[] = "/\\:*?\"<>|";
        char c = static_cast<char>(codepoint);
        bool safe = true;
        for (size_t i = 0; unsafe[i]; ++i) {
            if (c == unsafe[i]) { safe = false; break; }
        }
        if (safe) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%c.svg", c);
            return std::string(buf);
        }
    }
    // Everything else: U+XXXX or U+XXXXXX hex.
    char buf[32];
    if (codepoint <= 0xFFFF)
        snprintf(buf, sizeof(buf), "U+%04lX.svg", static_cast<unsigned long>(codepoint));
    else
        snprintf(buf, sizeof(buf), "U+%06lX.svg", static_cast<unsigned long>(codepoint));
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// Write a single glyph SVG to disk
// ---------------------------------------------------------------------------

static void writeGlyphSvg(const std::filesystem::path& outDir,
                           const std::string& filename,
                           FT_Face face) {
    FT_GlyphSlot slot = face->glyph;

    // Build the SVG path data by decomposing the outline.
    SvgPathBuilder builder;
    FT_Outline_Funcs funcs = builder.funcs();

    // Close each contour with Z before moving to the next.
    // FT_Outline_Decompose does NOT call move_to between contours automatically
    // for closing — we need to emit Z ourselves after each contour.
    // Strategy: decompose contour by contour.
    FT_Outline& outline = slot->outline;
    int ptStart = 0;
    for (int c = 0; c < outline.n_contours; c++) {
        int ptEnd = outline.contours[c]; // last point index of this contour (inclusive)
        int nPts  = ptEnd - ptStart + 1;

        FT_Outline sub;
        sub.n_points   = static_cast<short>(nPts);
        sub.n_contours = 1;
        sub.points     = outline.points  + ptStart;
        sub.tags       = outline.tags    + ptStart;
        // Contour's end index must be relative to the sub-outline's point array.
        short relEnd = static_cast<short>(nPts - 1);
        sub.contours = &relEnd;
        sub.flags    = outline.flags;

        FT_Error err = FT_Outline_Decompose(&sub, &funcs, &builder);
        assert(err == 0);

        builder.d << "Z ";
        ptStart = ptEnd + 1;
    }

    std::string pathData = builder.d.str();

    // Compute viewBox from face-level metrics (design units).
    // Use the face ascender/descender for a consistent vertical extent across glyphs.
    FT_Long ascender  =  face->ascender;
    FT_Long descender =  face->descender; // typically negative
    FT_Long height    =  ascender - descender;
    FT_Pos  advance   =  slot->metrics.horiAdvance;

    // viewBox: "minX minY width height" in design units with Y-up → Y-down flip via transform.
    // We place the baseline at y=0; ascender is above (positive Y in FT coords).
    // In SVG space after scale(1,-1), the visible area is minY=-ascender, height=height.
    char viewBox[128];
    snprintf(viewBox, sizeof(viewBox), "0 %ld %ld %ld",
             static_cast<long>(-ascender),
             static_cast<long>(advance),
             static_cast<long>(height));

    std::filesystem::path outPath = outDir / filename;
    std::ofstream out(outPath);
    assert(out.is_open());

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << viewBox << "\">\n";
    if (!pathData.empty()) {
        out << "  <path transform=\"scale(1,-1)\" d=\"" << pathData << "\"/>\n";
    }
    out << "</svg>\n";
}

// ---------------------------------------------------------------------------
// Process one OTF file
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

    // Use FT_LOAD_NO_SCALE so coordinates are in raw font design units.
    // No pixel size needed when loading with NO_SCALE.

    // Derive output directory: sibling of the .otf file, named after stem.
    std::filesystem::path outDir = otfPath.parent_path() / otfPath.stem();
    std::filesystem::create_directories(outDir);

    // Build a map from glyph index → Unicode codepoint using the best charmap.
    // We'll use FT_Get_First_Char / FT_Get_Next_Char which operate on the
    // currently selected charmap (FreeType selects a Unicode cmap by default).
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

    // Track which glyph indices we have already written (a codepoint may alias
    // an already-emitted glyph; skip duplicates).
    std::set<FT_UInt> written;
    int glyphsWritten = 0;

    for (FT_Long gi = 0; gi < face->num_glyphs; gi++) {
        FT_UInt gidx = static_cast<FT_UInt>(gi);

        err = FT_Load_Glyph(face, gidx, FT_LOAD_NO_BITMAP | FT_LOAD_NO_SCALE);
        if (err != 0) {
            fprintf(stderr, "  [%s] WARNING: could not load glyph %u (FT error %d), skipping\n",
                    familyName.c_str(), static_cast<unsigned>(gidx), err);
            continue;
        }

        // Only process glyphs with outline data (skip bitmap / no-outline glyphs).
        if (face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
            continue;
        }

        if (written.count(gidx)) {
            continue;
        }
        written.insert(gidx);

        std::string filename = glyphFilename(
            hasCp[gidx] ? indexToCodepoint[gidx] : 0,
            gidx,
            hasCp[gidx]);

        writeGlyphSvg(outDir, filename, face);
        glyphsWritten++;
    }

    printf("[%s] %s -> %s/ (%d glyphs)\n",
           familyName.c_str(),
           otfPath.filename().string().c_str(),
           otfPath.stem().string().c_str(),
           glyphsWritten);

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
