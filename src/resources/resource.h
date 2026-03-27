#pragma once

#include <SDL3/SDL.h>
#include "../core/Vector.h"
#include "../core/HashTable.h"
#include "../core/ResourceTypes.h"

// Forward declarations
class MemoryAllocator;
class ConsoleBuffer;

struct ResourceData {
    char* data;
    Uint64 size;
    Uint32 type;
};

// Atlas UV coordinates for texture
struct AtlasUV {
    Uint64 atlasId;   // ID of the atlas texture
    float u0, v0;       // Bottom-left UV
    float u1, v1;       // Top-right UV
    Uint16 width;     // Original image width
    Uint16 height;    // Original image height
};

class PakResource {
public:
    PakResource(MemoryAllocator* allocator, ConsoleBuffer* consoleBuffer);
    ~PakResource();
    bool load(const char* filename);
    bool reload(const char* filename);

    // Async-only resource API
    void requestResourceAsync(Uint64 id);
    void preloadAllResourcesAsync();
    bool tryGetResource(Uint64 id, ResourceData& outData);
    bool areAllResourcesReady();

    bool hasResource(Uint64 id);

    // Non-blocking atlas helpers (return false when still loading/not atlas)
    bool tryGetAtlasUV(Uint64 textureId, AtlasUV& uv);

    // Non-blocking atlas data access
    bool tryGetAtlasData(Uint64 atlasId, ResourceData& outData);

    bool isResourceReady(Uint64 id);

private:
    enum ResourceLoadState : uint8_t {
        RESOURCE_NOT_REQUESTED = 0,
        RESOURCE_QUEUED = 1,
        RESOURCE_LOADING = 2,
        RESOURCE_READY = 3,
        RESOURCE_FAILED = 4
    };

    static int resourceWorkerThread(void* data);
    bool loadResourceDataLocked(Uint64 id, ResourceData& outData);
    void clearResourceCacheLocked();
    void buildResourceIndexLocked();

    ResourceData m_pakData;
    Vector<char> m_pakFileBuffer;
    HashTable<Uint64, Vector<char>*> m_decompressedData;
    HashTable<Uint64, ResourcePtr> m_resourceIndex;
    HashTable<Uint64, ResourceData> m_loadedResourceData;
    HashTable<Uint64, uint8_t> m_resourceStates;
    Vector<Uint64> m_requestQueue;
    HashTable<Uint64, AtlasUV> m_atlasUVCache;  // Cache of atlas UV lookups
    SDL_Mutex* m_mutex;
    SDL_Condition* m_requestCondition;
    SDL_Thread* m_workerThread;
    bool m_workerRunning;
    MemoryAllocator* m_allocator;
    ConsoleBuffer* m_consoleBuffer;

};