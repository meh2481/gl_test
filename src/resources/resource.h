#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <map>
#include <SDL3/SDL.h>

struct ResourceData {
    char* data;
    size_t size;
    uint32_t type;
};

// Atlas UV coordinates for texture
struct AtlasUV {
    uint64_t atlasId;   // ID of the atlas texture
    float u0, v0;       // Bottom-left UV
    float u1, v1;       // Top-right UV
    uint16_t width;     // Original image width
    uint16_t height;    // Original image height
};

class PakResource {
public:
    PakResource();
    ~PakResource();
    bool load(const char* filename);
    bool reload(const char* filename);
    ResourceData getResource(uint64_t id);

    // Get atlas UV coordinates for a texture resource
    // Returns true if the resource is an atlas reference, false if standalone
    bool getAtlasUV(uint64_t textureId, AtlasUV& uv);

    // Get the actual atlas image data for rendering
    ResourceData getAtlasData(uint64_t atlasId);

    // Async resource loading - preloads and decompresses resources in background threads
    // Use preloadResourceAsync() to start loading, then isResourceReady() to check completion
    void preloadResourceAsync(uint64_t id);
    bool isResourceReady(uint64_t id);

private:
    ResourceData m_pakData;
    std::map<uint64_t, std::vector<char>> m_decompressedData;
    std::map<uint64_t, AtlasUV> m_atlasUVCache;  // Cache of atlas UV lookups
    SDL_Mutex* m_mutex;

#ifdef _WIN32
    HANDLE m_hFile;
    HANDLE m_hMapping;
#else
    int m_fd;
#endif
};