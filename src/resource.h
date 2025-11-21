#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <map>
#include <SDL3/SDL.h>

struct ResourceData {
    char* data;
    size_t size;
};

class PakResource {
public:
    PakResource();
    ~PakResource();
    bool load(const char* filename);
    bool reload(const char* filename);
    ResourceData getResource(uint64_t id);

    // Async resource loading - preloads and decompresses resources in background threads
    // Use preloadResourceAsync() to start loading, then isResourceReady() to check completion
    void preloadResourceAsync(uint64_t id);
    bool isResourceReady(uint64_t id);

private:
    ResourceData m_pakData;
    std::map<uint64_t, std::vector<char>> m_decompressedData;
    SDL_Mutex* m_mutex;

#ifdef _WIN32
    HANDLE m_hFile;
    HANDLE m_hMapping;
#else
    int m_fd;
#endif
};