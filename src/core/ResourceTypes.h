#pragma once

#include <SDL3/SDL_stdinc.h>

#define VERSION_1_0        1

#define f32_t float
#define f64_t double

//------------------------------------
// Header of the .pak file
//------------------------------------
typedef struct
{
    char sig[4];        //PAKC in big endian for current version
    Uint32 version;    //VERSION_1_0 for current version of the game
    Uint32 numResources;
    Uint32 pad;
    //Followed by numResources ResourcePtrs
} PakFileHeader;


//------------------------------------
// Pointer to resource within file
//------------------------------------

typedef struct
{
    Uint64 id;        //Resource ID
    Uint64 offset;    //Offset from start of file to CompressionHeader
    Uint64 lastModified; //Unix timestamp of the last time this was modified prior to packing
} ResourcePtr;


//--------------------------------------------------------------
// Compressed data
//--------------------------------------------------------------
#define COMPRESSION_FLAGS_UNCOMPRESSED    0
#define COMPRESSION_FLAGS_CMPR            1

typedef struct
{
    Uint32 compressionType;    //One of the compression flags above
    Uint32 compressedSize;
    Uint32 decompressedSize;
    Uint32 type;                //One of the resource types below
    //Followed by compressed data
} CompressionHeader;

#define RESOURCE_TYPE_UNKNOWN       0   //Default for don't care
#define RESOURCE_TYPE_IMAGE         1
#define RESOURCE_TYPE_IMAGE_ATLAS   2
#define RESOURCE_TYPE_SOUND         3
#define RESOURCE_TYPE_MUSIC_TRACK   4  //Layered music track with loop points and intensities
#define RESOURCE_TYPE_FONT          5
#define RESOURCE_TYPE_STRINGBANK    6
#define RESOURCE_TYPE_OBJ           7   //3D object (linking between a 3D mesh and a texture)
#define RESOURCE_TYPE_MESH          8   //3D mesh
#define RESOURCE_TYPE_JSON          9
#define RESOURCE_TYPE_LUA           10  //Lua script
#define RESOURCE_TYPE_IMAGE_NO_ATLAS 11  //Icon image or other image without atlas
#define RESOURCE_TYPE_SHADER        12
#define RESOURCE_TYPE_TRIG_TABLE    13  //Trig lookup table (sin/cos)
#define RESOURCE_TYPE_VECTOR_SHAPE  14  //Vector shape (analytic SDF, cubic Bezier curves)
#define RESOURCE_TYPE_DIALOGUE      15  //Binary dialogue resource
#define RESOURCE_TYPE_CHARACTER     16  //Binary character definition resource
//#define RESOURCE_TYPE_
//etc


//--------------------------------------------------------------
// Textures
//--------------------------------------------------------------
typedef struct //Structure for texture atlas data
{
    Uint16 format;        //Image format (see IMAGE_FORMAT_* constants below)
    Uint16 width;         //Width of atlas in pixels
    Uint16 height;        //Height of atlas in pixels
    Uint16 numEntries;    //Number of images packed into this atlas
    //Followed by numEntries AtlasEntry structures
    //Followed by compressed image data
} AtlasHeader;

typedef struct //Structure for individual image entry in atlas
{
    Uint64 originalId;    //Original resource ID of the packed image
    Uint16 x;             //X position in atlas (pixels)
    Uint16 y;             //Y position in atlas (pixels)
    Uint16 width;         //Width of image in atlas (pixels)
    Uint16 height;        //Height of image in atlas (pixels)
} AtlasEntry;

typedef struct //Structure for image indices into the atlas AtlasHeader
{
    Uint64 atlasId;    //ID of AtlasHeader
    f32_t coordinates[8];    //UV texture coordinates for the image in the atlas
} TextureHeader;

typedef struct //Structure for (non-atlased) image data
{
    Uint16 format;        //Image format (see IMAGE_FORMAT_* constants below)
    Uint16 width;         //Width of image
    Uint16 height;        //Height of image
    Uint16 pad;
                           //Followed by image data
} ImageHeader;

// Image format constants for ImageHeader.format
// Most modern desktop GPUs support BC3/DXT5 compression, most mobile GPUs support ETC2 compression
#define IMAGE_FORMAT_RAW_RGBA       0   // Uncompressed RGBA (4 bytes per pixel)
#define IMAGE_FORMAT_RAW_RGB        1   // Uncompressed RGB (3 bytes per pixel)
#define IMAGE_FORMAT_BC1_DXT1       2   // BC1/DXT1 compression (RGB, no alpha, 0.5 bytes per pixel)
#define IMAGE_FORMAT_BC3_DXT5       3   // BC3/DXT5 compression (RGBA with alpha, 1 byte per pixel)
#define IMAGE_FORMAT_ETC1           4   // ETC1 compression (RGB, no alpha, 0.5 bytes per pixel)
#define IMAGE_FORMAT_ETC2           5   // ETC2 compression (RGBA with alpha, 1 byte per pixel)
#define IMAGE_FORMAT_ASTC_4x4       6   // ASTC 4x4 compression (RGBA with alpha, 1 byte per pixel)

// Default maximum atlas texture size (can be overridden at pack time)
// Most desktop GPUs support at least 4096x4096 textures
#define DEFAULT_ATLAS_MAX_SIZE      4096


//--------------------------------------------------------------
// Fonts
//--------------------------------------------------------------

// Binary analytic-SDF font resource (RESOURCE_TYPE_FONT, produced by font_extractor).
//
// Version 2 binary layout:
//   FontBinaryHeader                        (32 bytes)
//   FontGlyphEntryDisk[numGlyphs]           (16 bytes each, sorted by glyph index 0..numGlyphs-1)
//   FontKernPairDisk[numKernPairs]          (8 bytes each, sorted by (left,right) glyph index)
//   [Compact SDF section]                   -- referenced by FontGlyphEntryDisk.sdfOffset
//
// Each glyph's compact SDF blob layout:
//   CompactShapeHeader                      (12 bytes)
//   CompactContourHeader[numContours]       (2 bytes each)
//   [Segment stream]                        -- variable-length typed records
//
// Segment stream for each contour:
//   Sint16[2]                               -- explicit start point p0 (x, y)
//   For each segment (numSegments total):
//     Uint8 type                            -- FONT_SEG_LINE or FONT_SEG_CUBIC
//     If FONT_SEG_LINE:  Sint16[2] p3       -- endpoint only; p0 implicit from chain
//     If FONT_SEG_CUBIC: Sint16[6] p1,p2,p3 -- three new control points; p0 implicit
//
// All coordinates are normalised by unitsPerEM and encoded as:
//   int16_value = round(normalised_float * FONT_COORD_SCALE)
//
// At load time FontManager decodes these back to SdfShapeHeader + SdfContourHeader[]
// + SdfSegment[] so the GPU upload path (VulkanRenderer::loadVectorShape) is unchanged.

// Scale factor for encoding normalised glyph coordinates as Sint16.
// Sint16 range [-32768, 32767] covers normalised values in [-2.0, ~2.0], which is
// sufficient for any real font (Aileron coords are in [-0.23, 1.06]).
#define FONT_COORD_SCALE     16384.0f

// Compact segment type tags written into the segment stream.
#define FONT_SEG_LINE        0u   // line: 1-byte type + 2*Sint16 endpoint
#define FONT_SEG_CUBIC       1u   // cubic Bezier: 1-byte type + 6*Sint16 (p1,p2,p3)

typedef struct
{
    Uint32 numGlyphs;       // number of FontGlyphEntryDisk records (== face->num_glyphs)
    Uint32 numKernPairs;    // number of FontKernPairDisk records
    Sint32 unitsPerEM;      // font design units per em square
    Sint32 ascender;        // typical ascender height in design units (positive)
    Sint32 descender;       // typical descender depth in design units (negative)
    Sint32 lineGap;         // extra leading between lines in design units
} FontBinaryHeader;

// On-disk glyph entry (16 bytes).  Entries are stored in glyph-index order so
// the array position equals the font-internal glyph index (no field needed).
typedef struct
{
    Uint32 codepoint;       // Unicode codepoint (0 for glyphs not in any charmap)
    Sint16 advanceWidth;    // horizontal advance in design units
    Sint16 leftBearing;     // left-side bearing in design units
    Uint32 sdfOffset;       // byte offset into compact SDF section (0 if sdfSize == 0)
    Uint32 sdfSize;         // byte size of compact SDF blob (0 = glyph has no outline)
} FontGlyphEntryDisk;

// On-disk kern pair (8 bytes).
typedef struct
{
    Uint16 leftGlyphIndex;
    Uint16 rightGlyphIndex;
    Sint32 kernValue;       // kern adjustment in design units (negative = tighten)
} FontKernPairDisk;

// Compact SDF shape header (12 bytes).
typedef struct
{
    Uint16 numContours;     // number of CompactContourHeader entries that follow
    Uint16 totalSegments;   // total segments across all contours
    Sint16 bboxMinX;        // tight bounding box (FONT_COORD_SCALE units)
    Sint16 bboxMinY;
    Sint16 bboxMaxX;
    Sint16 bboxMaxY;
} CompactShapeHeader;

// Per-contour header in compact SDF (2 bytes).
typedef struct
{
    Uint16 numSegments;     // number of segments in this contour
} CompactContourHeader;

// In-memory glyph entry used by FontManager and TextLayout (unchanged public struct).
// Populated at load time by decoding FontGlyphEntryDisk[].
// glyphIndex is set to the array position (== font-internal glyph index).
// advanceWidth and leftBearing are sign-extended from the on-disk Sint16 values.
typedef struct
{
    Uint32 codepoint;       // Unicode codepoint (0 for glyphs not in any charmap)
    Uint32 glyphIndex;      // font-internal glyph index (= array position in loaded font)
    Sint32 advanceWidth;    // horizontal advance in design units
    Sint32 leftBearing;     // left-side bearing in design units
    Uint32 sdfOffset;       // byte offset into decoded SDF buffer (0 if sdfSize == 0)
    Uint32 sdfSize;         // byte size of decoded SDF blob (0 = glyph has no outline)
} FontGlyphEntry;

// In-memory kern pair used by FontManager (unchanged public struct).
// Populated at load time by decoding FontKernPairDisk[].
typedef struct
{
    Uint32 leftGlyphIndex;
    Uint32 rightGlyphIndex;
    Sint32 kernValue;       // kern adjustment in design units (negative = tighten)
} FontKernPair;

//--------------------------------------------------------------
// Text data
//--------------------------------------------------------------
//String bank file format
typedef struct
{
    Uint32 numStrings;
    Uint32 numLanguages;
    //Followed by numLangages LanguageOffsets
    //Followed by numStrings StringIDs (sorted from least to greatest)
    //Followed by numStrings*numLanguages StringDataPointers
    //Followed by actual string data
} StringBankHeader;

//See https://www.loc.gov/standards/iso639-2/php/code_list.php for ISO 639 codes
typedef struct
{
    char languageID[4];    //ISO 639-1 (if existing) or 639-2 language code in all-lowercase (en for English, es for Spanish, etc). Should only be two or three chars, others '\0'
    Uint32 offset;    //Offset from first StringDataPointer for a string's ID to the StringDataPointer for this language

    //For example, for languages en=0 and es=1, for a StringID that's number 7 in the list (8th entry),
    //en's StringDataPointer is number 7*2+0=14 and the English string can be found at that pointer's offset
    //es's StringDataPointer is number 7*2+1=15 and the Spanish string can be found at that pointer's offset

} LanguageOffset;

typedef struct
{
    Uint64 id;        //ID of the string
} StringID;

typedef struct
{
    Uint64 offset;    //Offset from start of string data to the start of the actual, null-terminated string
} StringDataPointer;

//--------------------------------------------------------------
// Music track data (RESOURCE_TYPE_MUSIC_TRACK)
// Layered music with multiple intensities and loop points.
// Binary layout:
//   MusicTrackHeader
//   MusicIntensityInfo[numIntensities]
//   Uint64 layerIds[totalLayers]   -- flat array, grouped by intensity in layerStartIndex order
//--------------------------------------------------------------
typedef struct
{
    Uint32 loopStartSample;   //Loop start in audio samples at 48kHz (inclusive)
    Uint32 loopEndSample;     //Loop end in audio samples at 48kHz (exclusive, 0 = use file end)
    Uint32 numIntensities;    //Number of intensity entries that follow
    Uint32 totalLayers;       //Total layer IDs stored across all intensities
    //Followed by numIntensities MusicIntensityInfo structs
    //Followed by totalLayers Uint64 layer resource IDs
} MusicTrackHeader;

typedef struct
{
    Uint64 nameHash;          //FNV-1a hash of the intensity name string
    Uint32 layerStartIndex;   //Index of first layer ID in the flat layerIds array
    Uint32 numLayers;         //Number of layer resource IDs for this intensity
    Uint32 pad;
} MusicIntensityInfo;

//--------------------------------------------------------------
// Mesh data
//--------------------------------------------------------------
typedef struct
{
    Uint32 numVertices;
    //Followed by vertex data
    //Followed by texture coordinate data
    //Followed by normal data
} MeshHeader;

typedef struct
{
    Uint64 meshId;    //ID of MeshHeader
    Uint64 textureId;    //ID of TextureHeader
} Object3DHeader;

//--------------------------------------------------------------
// Trigonometry lookup table
//--------------------------------------------------------------
// Trig lookup table with entries for every half-degree (0.5°)
// This gives us 720 entries to cover 0-360 degrees (0 to 2*PI radians)
typedef struct
{
    Uint32 numEntries;    // Should be 720 (360 / 0.5)
    f32_t angleStep;        // Radians per entry (PI/360)
    // Followed by numEntries floats for sin values
    // Followed by numEntries floats for cos values
} TrigTableHeader;

//--------------------------------------------------------------
// GLA audio data (RESOURCE_TYPE_SOUND)
// Custom IMA ADPCM audio format for zero-decode-cost playback via AL_EXT_IMA4.
// Fixed block parameters compatible with OpenAL Soft's AL_EXT_IMA4 default:
//   Mono   block: 36 bytes, 65 samples per block
//   Stereo block: 72 bytes, 65 samples per channel per block
// Binary layout:
//   GlaHeader
//   IMA ADPCM blocks (each GlaHeader.blockSizeBytes bytes)
//--------------------------------------------------------------
#define GLA_VERSION           1
#define GLA_SAMPLES_PER_BLOCK 65
#define GLA_MONO_BLOCK_BYTES  36
#define GLA_STEREO_BLOCK_BYTES 72

typedef struct
{
    char     sig[4];            // "GLAD" (GL Audio Data)
    Uint32   version;           // GLA_VERSION
    Uint32   sampleRate;        // e.g. 48000
    Uint16   channels;          // 1 (mono) or 2 (stereo)
    Uint16   blockSizeBytes;    // GLA_MONO_BLOCK_BYTES or GLA_STEREO_BLOCK_BYTES
    Uint32   samplesPerBlock;   // GLA_SAMPLES_PER_BLOCK
    Uint32   totalSamples;      // Total PCM samples per channel in the file
    Uint32   loopStart;         // Loop start in samples (0 = start of file)
    Uint32   loopEnd;           // Loop end in samples (0 = use file end)
    // Followed immediately by IMA ADPCM blocks
} GlaHeader;

//--------------------------------------------------------------
// Vector shape data (RESOURCE_TYPE_VECTOR_SHAPE)
// Analytic SDF representation of cubic Bézier curves for resolution-
// independent GPU rendering.
//
// Binary layout:
//   SdfShapeHeader
//   SdfContourHeader[numContours]
//   SdfSegment[totalSegments]
//
// All X,Y coordinates are normalised so the largest dimension spans
// [-0.5, 0.5].  Y is negated (SVG Y-down → world Y-up).
//--------------------------------------------------------------

// One cubic Bézier segment stored as its four control points.
typedef struct
{
    float p0x, p0y;   // Start point
    float p1x, p1y;   // First control point
    float p2x, p2y;   // Second control point
    float p3x, p3y;   // End point
} SdfSegment;

// Per-contour metadata; followed by numSegments SdfSegments in the flat
// segment array starting at segmentOffset.
typedef struct
{
    Uint32 numSegments;    // Number of SdfSegment entries for this contour
    Sint32 winding;        // +1 = outer contour, -1 = hole
    Uint32 segmentOffset;  // Index of first segment in the flat SdfSegment array
    Uint32 pad;
} SdfContourHeader;

typedef struct
{
    Uint32 numContours;     // Number of SdfContourHeader entries
    float  bboxMinX;        // Tight bounding box of control points (normalised coords)
    float  bboxMinY;
    float  bboxMaxX;
    float  bboxMaxY;
    Uint32 totalSegments;   // Total SdfSegment entries across all contours
    Uint32 pad[2];
    // Followed by numContours * SdfContourHeader
    // Followed by totalSegments * SdfSegment
} SdfShapeHeader;

//--------------------------------------------------------------
// Dialogue data (RESOURCE_TYPE_DIALOGUE)
// Binary dialogue resource with optional multi-language support.
// The pak file CompressionHeader provides type validation;
// no magic number or version field is needed here.
//
// Binary layout:
//   DialogueBinaryHeader
//   DialogueLanguageEntry[languageCount]   -- ISO language code table
//   DialogueLineRecord[lineCount * languageCount]
//     -- ordered language-major: all lines for language 0,
//        then all lines for language 1, etc.
//--------------------------------------------------------------
typedef struct
{
    Uint32 lineCount;      // number of dialogue lines per language
    Uint32 languageCount;  // number of language variants (>= 1)
} DialogueBinaryHeader;

// Maximum field sizes for dialogue line string data.
#define DIALOGUE_MAX_TEXT      1024
#define DIALOGUE_MAX_SHORT      256
#define DIALOGUE_LANG_CODE_LEN    8   // ISO code buffer (e.g. "en", "es")
#define DIALOGUE_PORTRAIT_TAG_LEN 64  // portrait identifier (e.g. "wave")

// One entry per language, immediately after DialogueBinaryHeader.
typedef struct
{
    char code[DIALOGUE_LANG_CODE_LEN]; // null-terminated ISO code, e.g. "en"
} DialogueLanguageEntry;

typedef struct
{
    Uint64 characterId;                          // resource ID of character def (0 = anonymous)
    char   portraitTag[DIALOGUE_PORTRAIT_TAG_LEN]; // which portrait to show (matches CharacterPortraitEntry.tag)
    Uint8  portraitSide;                         // 0 = left, 1 = right
    Uint8  pad0[3];                              // alignment padding
    char   text[DIALOGUE_MAX_TEXT];              // body text (may contain markup)
    char   revealSoundPath[DIALOGUE_MAX_SHORT];  // optional per-char reveal sound override (empty = use character default)
    char   voicePath[DIALOGUE_MAX_SHORT];        // optional voiced line audio path
    float  revealSpeed;                          // chars per second (0 = inherit from box)
    Uint32 pad1;                                 // alignment padding
} DialogueLineRecord;

//--------------------------------------------------------------
// Character definition data (RESOURCE_TYPE_CHARACTER)
// Binary character definition resource.
//
// Binary layout:
//   CharacterBinaryHeader
//   CharacterPortraitEntry[numPortraits]
//--------------------------------------------------------------

#define CHARACTER_MAX_NAME    256  // display name buffer
#define CHARACTER_MAX_PATH    256  // resource path buffer
#define CHARACTER_MAX_TAG      64  // portrait tag buffer

typedef struct
{
    Uint32 numPortraits;                   // number of CharacterPortraitEntry entries that follow
    Uint32 nameColor;                      // RGBA packed default name colour (0xRRGGBBAA)
    float  revealSpeed;                    // default reveal speed in chars/sec (0 = inherit from box)
    char   revealSoundPath[CHARACTER_MAX_PATH]; // default per-char reveal sound resource path
    char   speakerName[CHARACTER_MAX_NAME];     // display name for this character
} CharacterBinaryHeader;

typedef struct
{
    char tag[CHARACTER_MAX_TAG];           // portrait identifier (e.g. "wave", "smile")
    char resourcePath[CHARACTER_MAX_PATH]; // image resource path
} CharacterPortraitEntry;
