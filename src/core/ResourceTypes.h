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
#define RESOURCE_TYPE_VECTOR_SHAPE  14  //Vector shape (Loop-Blinn tessellated mesh)
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

typedef struct
{
    Uint32 numChars;
    Uint64 textureId;    //ID of texture resource to use
    Uint32 pad;
    //Followed by numChars Uint32's (aka 32-bit UTF-8 codepoints), sorted from lowest to highest
    //Followed by numChars * 8 floats (the UV coordinate rectangles for the characters, each float in range [0..1])
} FontHeader;

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
// Loop-Blinn tessellated mesh for GPU-side vector rendering.
// Binary layout:
//   VectorShapeHeader
//   VectorVertex[numVertices]
//   Uint16[numIndices]
//--------------------------------------------------------------
typedef struct
{
    float x;      // World position X
    float y;      // World position Y
    float k;      // Loop-Blinn k coordinate (0 = fill triangle)
    float l;      // Loop-Blinn l coordinate (0 = fill triangle)
    float m;      // Loop-Blinn m coordinate (0 = fill triangle)
    float sign;   // 0 = fill; +1 = convex curve (keep inside parabola); -1 = concave curve
} VectorVertex;

typedef struct
{
    Uint32 numVertices;   // Number of VectorVertex entries following header
    Uint32 numIndices;    // Number of Uint16 index entries following vertices
    float  bboxMinX;      // Bounding box in NORMALISED [-0.5, 0.5] coordinates
    float  bboxMinY;
    float  bboxMaxX;
    float  bboxMaxY;
    Uint32 pad[2];
    // Followed by numVertices * VectorVertex (already in normalised coordinates)
    // Followed by numIndices  * Uint16
} VectorShapeHeader;
