#pragma once

#include <cstdint>
#include <SDL3/SDL.h>
#include "../core/Vector.h"
#include "../core/HashTable.h"
#include "../core/ResourceTypes.h"

// Forward declarations
class MemoryAllocator;
class ConsoleBuffer;

struct ResourceData {
    char* data;
    uint64_t size;
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
    PakResource(MemoryAllocator* allocator, ConsoleBuffer* consoleBuffer);
    ~PakResource();
    bool load(const char* filename);
    bool reload(const char* filename);

    // Async-only resource API
    void requestResourceAsync(uint64_t id);
    void preloadAllResourcesAsync();
    bool tryGetResource(uint64_t id, ResourceData& outData);
    bool areAllResourcesReady();

    bool hasResource(uint64_t id);

    // Non-blocking atlas helpers (return false when still loading/not atlas)
    bool tryGetAtlasUV(uint64_t textureId, AtlasUV& uv);

    // Non-blocking atlas data access
    bool tryGetAtlasData(uint64_t atlasId, ResourceData& outData);

    bool isResourceReady(uint64_t id);

private:
    enum ResourceLoadState : uint8_t {
        RESOURCE_NOT_REQUESTED = 0,
        RESOURCE_QUEUED = 1,
        RESOURCE_LOADING = 2,
        RESOURCE_READY = 3,
        RESOURCE_FAILED = 4
    };

    static int resourceWorkerThread(void* data);
    bool loadResourceDataLocked(uint64_t id, ResourceData& outData);
    void clearResourceCacheLocked();
    void buildResourceIndexLocked();

    ResourceData m_pakData;
    Vector<char> m_pakFileBuffer;
    HashTable<uint64_t, Vector<char>*> m_decompressedData;
    HashTable<uint64_t, ResourcePtr> m_resourceIndex;
    HashTable<uint64_t, ResourceData> m_loadedResourceData;
    HashTable<uint64_t, uint8_t> m_resourceStates;
    Vector<uint64_t> m_requestQueue;
    HashTable<uint64_t, AtlasUV> m_atlasUVCache;  // Cache of atlas UV lookups
    SDL_Mutex* m_mutex;
    SDL_Condition* m_requestCondition;
    SDL_Thread* m_workerThread;
    bool m_workerRunning;
    MemoryAllocator* m_allocator;
    ConsoleBuffer* m_consoleBuffer;

};