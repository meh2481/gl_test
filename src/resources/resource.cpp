#include "resource.h"
#include "../core/ResourceTypes.h"
#include "../core/Vector.h"
#include "../debug/ConsoleBuffer.h"
#include "../debug/ThreadProfiler.h"
#include <cassert>
#include <lz4.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

PakResource::PakResource(MemoryAllocator* allocator, ConsoleBuffer* consoleBuffer)
    : m_pakData{nullptr, 0}
    , m_decompressedData(*allocator, "PakResource::m_decompressedData")
    , m_resourceIndex(*allocator, "PakResource::m_resourceIndex")
    , m_loadedResourceData(*allocator, "PakResource::m_loadedResourceData")
    , m_resourceStates(*allocator, "PakResource::m_resourceStates")
    , m_requestQueue(*allocator, "PakResource::m_requestQueue")
    , m_atlasUVCache(*allocator, "PakResource::m_atlasUVCache")
    , m_requestCondition(nullptr)
    , m_workerThread(nullptr)
    , m_workerRunning(true)
    , m_allocator(allocator)
    , m_consoleBuffer(consoleBuffer)
#ifdef _WIN32
    , m_hFile(INVALID_HANDLE_VALUE)
    , m_hMapping(NULL)
#else
    , m_fd(-1)
#endif
{
    assert(m_allocator != nullptr);
    assert(m_consoleBuffer != nullptr);
    m_mutex = SDL_CreateMutex();
    assert(m_mutex != nullptr);
    m_requestCondition = SDL_CreateCondition();
    assert(m_requestCondition != nullptr);
    m_workerThread = SDL_CreateThread(resourceWorkerThread, "ResourceWorker", this);
    assert(m_workerThread != nullptr);
}

PakResource::~PakResource() {
    if (m_mutex && m_requestCondition) {
        SDL_LockMutex(m_mutex);
        m_workerRunning = false;
        SDL_SignalCondition(m_requestCondition);
        SDL_UnlockMutex(m_mutex);
    }

    if (m_workerThread) {
        SDL_WaitThread(m_workerThread, nullptr);
        m_workerThread = nullptr;
    }

    if (m_requestCondition) {
        SDL_DestroyCondition(m_requestCondition);
        m_requestCondition = nullptr;
    }

    // Clean up decompressed data
    for (auto it = m_decompressedData.begin(); it != m_decompressedData.end(); ++it) {
        Vector<char>* vec = it.value();
        vec->~Vector<char>();
        m_allocator->free(vec);
    }
    m_decompressedData.clear();
    m_resourceIndex.clear();
    m_loadedResourceData.clear();
    m_resourceStates.clear();
    m_requestQueue.clear();
    if (m_pakData.data) {
#ifdef _WIN32
        if (m_pakData.data) UnmapViewOfFile(m_pakData.data);
        if (m_hMapping) CloseHandle(m_hMapping);
        if (m_hFile != INVALID_HANDLE_VALUE) CloseHandle(m_hFile);
#else
        munmap(m_pakData.data, m_pakData.size);
        if (m_fd != -1) close(m_fd);
#endif
    }
    if (m_mutex) {
        SDL_DestroyMutex(m_mutex);
    }
}

bool PakResource::load(const char* filename) {
    if (m_pakData.data) return true; // already loaded
#ifdef _WIN32
    m_hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_hFile == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(m_hFile, NULL);
    m_hMapping = CreateFileMapping(m_hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!m_hMapping) {
        CloseHandle(m_hFile);
        m_hFile = INVALID_HANDLE_VALUE;
        return false;
    }
    void* addr = MapViewOfFile(m_hMapping, FILE_MAP_READ, 0, 0, 0);
    if (!addr) {
        CloseHandle(m_hMapping);
        m_hMapping = NULL;
        CloseHandle(m_hFile);
        m_hFile = INVALID_HANDLE_VALUE;
        return false;
    }
    m_pakData = ResourceData{(char*)addr, size};
#else
    m_fd = open(filename, O_RDONLY);
    if (m_fd == -1) return false;
    struct stat sb;
    if (fstat(m_fd, &sb) == -1) {
        close(m_fd);
        m_fd = -1;
        return false;
    }
    uint64_t size = sb.st_size;
    void* addr = mmap(NULL, size, PROT_READ, MAP_PRIVATE, m_fd, 0);
    if (addr == MAP_FAILED) {
        close(m_fd);
        m_fd = -1;
        return false;
    }
    m_pakData = ResourceData{(char*)addr, size};
#endif

    SDL_LockMutex(m_mutex);
    buildResourceIndexLocked();
    SDL_UnlockMutex(m_mutex);

    return true;
}

bool PakResource::reload(const char* filename) {
    SDL_LockMutex(m_mutex);

    clearResourceCacheLocked();

    if (m_pakData.data) {
#ifdef _WIN32
        UnmapViewOfFile(m_pakData.data);
        if (m_hMapping) CloseHandle(m_hMapping);
        if (m_hFile != INVALID_HANDLE_VALUE) CloseHandle(m_hFile);
        m_hFile = INVALID_HANDLE_VALUE;
        m_hMapping = NULL;
#else
        munmap(m_pakData.data, m_pakData.size);
        if (m_fd != -1) close(m_fd);
        m_fd = -1;
#endif
        m_pakData = {nullptr, 0};
    }

    SDL_UnlockMutex(m_mutex);

    return load(filename);
}

void PakResource::clearResourceCacheLocked() {
    for (auto it = m_decompressedData.begin(); it != m_decompressedData.end(); ++it) {
        Vector<char>* vec = it.value();
        vec->~Vector<char>();
        m_allocator->free(vec);
    }
    m_decompressedData.clear();
    m_loadedResourceData.clear();
    m_resourceStates.clear();
    m_resourceIndex.clear();
    m_requestQueue.clear();
    m_atlasUVCache.clear();
}

void PakResource::buildResourceIndexLocked() {
    m_resourceIndex.clear();
    m_loadedResourceData.clear();
    m_resourceStates.clear();
    m_requestQueue.clear();

    if (!m_pakData.data) {
        return;
    }

    PakFileHeader* header = (PakFileHeader*)m_pakData.data;
    if (memcmp(header->sig, "PAKC", 4) != 0) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "Invalid pak file signature");
        return;
    }

    ResourcePtr* ptrs = (ResourcePtr*)(m_pakData.data + sizeof(PakFileHeader));
    for (uint32_t i = 0; i < header->numResources; i++) {
        m_resourceIndex.insert(ptrs[i].id, ptrs[i]);
        m_resourceStates.insert(ptrs[i].id, RESOURCE_NOT_REQUESTED);
    }
}

bool PakResource::loadResourceDataLocked(uint64_t id, ResourceData& outData) {
    ResourceData* loaded = m_loadedResourceData.find(id);
    if (loaded != nullptr) {
        outData = *loaded;
        return true;
    }

    if (!m_pakData.data) {
        return false;
    }

    ResourcePtr* ptr = m_resourceIndex.find(id);
    if (ptr == nullptr) {
        return false;
    }

    CompressionHeader* comp = (CompressionHeader*)(m_pakData.data + ptr->offset);
    char* compressedData = (char*)(comp + 1);

    if (comp->compressionType == COMPRESSION_FLAGS_UNCOMPRESSED) {
        outData = ResourceData{compressedData, comp->decompressedSize, comp->type};
        m_loadedResourceData.insert(id, outData);
        return true;
    }

    if (comp->compressionType != COMPRESSION_FLAGS_LZ4) {
        return false;
    }

    Vector<char>** cachedDataPtr = m_decompressedData.find(id);
    if (cachedDataPtr != nullptr) {
        outData = ResourceData{(char*)(*cachedDataPtr)->data(), comp->decompressedSize, comp->type};
        m_loadedResourceData.insert(id, outData);
        return true;
    }

    void* vecMem = m_allocator->allocate(sizeof(Vector<char>), "PakResource::loadResourceDataLocked::Vector");
    Vector<char>* decompressed = new (vecMem) Vector<char>(*m_allocator, "PakResource::loadResourceDataLocked::decompressed");
    decompressed->resize(comp->decompressedSize);

    int result = LZ4_decompress_safe(compressedData, decompressed->data(), comp->compressedSize, comp->decompressedSize);
    if (result != (int)comp->decompressedSize) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "LZ4 decompression failed for resource %llu", (unsigned long long)id);
        decompressed->~Vector<char>();
        m_allocator->free(vecMem);
        return false;
    }

    m_decompressedData.insertNew(id, decompressed);
    outData = ResourceData{(char*)decompressed->data(), comp->decompressedSize, comp->type};
    m_loadedResourceData.insert(id, outData);
    return true;
}

int PakResource::resourceWorkerThread(void* data) {
    PakResource* resource = (PakResource*)data;
    assert(resource != nullptr);

    ThreadProfiler& profiler = ThreadProfiler::instance();
    profiler.registerThread("ResourceWorker");

    while (true) {
        profiler.updateThreadState(THREAD_STATE_WAITING);
        SDL_LockMutex(resource->m_mutex);

        while (resource->m_workerRunning && resource->m_requestQueue.empty()) {
            SDL_WaitCondition(resource->m_requestCondition, resource->m_mutex);
        }

        if (!resource->m_workerRunning && resource->m_requestQueue.empty()) {
            SDL_UnlockMutex(resource->m_mutex);
            break;
        }

        uint64_t id = resource->m_requestQueue[0];
        resource->m_requestQueue.erase(0);
        resource->m_resourceStates.insert(id, RESOURCE_LOADING);

        profiler.updateThreadState(THREAD_STATE_BUSY);
        ResourceData outData{nullptr, 0, 0};
        bool loaded = resource->loadResourceDataLocked(id, outData);
        resource->m_resourceStates.insert(id, loaded ? RESOURCE_READY : RESOURCE_FAILED);
        SDL_UnlockMutex(resource->m_mutex);
    }

    return 0;
}

void PakResource::requestResourceAsync(uint64_t id) {
    SDL_LockMutex(m_mutex);

    if (!m_pakData.data) {
        SDL_UnlockMutex(m_mutex);
        return;
    }

    if (!m_resourceIndex.contains(id)) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "Resource %llu not found in pak", (unsigned long long)id);
        SDL_UnlockMutex(m_mutex);
        return;
    }

    uint8_t* state = m_resourceStates.find(id);
    uint8_t currentState = (state != nullptr) ? *state : RESOURCE_NOT_REQUESTED;
    if (currentState == RESOURCE_READY || currentState == RESOURCE_LOADING || currentState == RESOURCE_QUEUED) {
        SDL_UnlockMutex(m_mutex);
        return;
    }

    m_resourceStates.insert(id, RESOURCE_QUEUED);
    m_requestQueue.push_back(id);
    SDL_SignalCondition(m_requestCondition);

    SDL_UnlockMutex(m_mutex);
}

void PakResource::preloadAllResourcesAsync() {
    SDL_LockMutex(m_mutex);

    for (auto it = m_resourceIndex.begin(); it != m_resourceIndex.end(); ++it) {
        uint64_t id = it.key();
        uint8_t* state = m_resourceStates.find(id);
        uint8_t currentState = (state != nullptr) ? *state : RESOURCE_NOT_REQUESTED;
        if (currentState == RESOURCE_READY || currentState == RESOURCE_LOADING || currentState == RESOURCE_QUEUED) {
            continue;
        }
        m_resourceStates.insert(id, RESOURCE_QUEUED);
        m_requestQueue.push_back(id);
    }

    SDL_SignalCondition(m_requestCondition);
    SDL_UnlockMutex(m_mutex);
}

bool PakResource::tryGetResource(uint64_t id, ResourceData& outData) {
    outData = ResourceData{nullptr, 0, 0};

    SDL_LockMutex(m_mutex);

    ResourceData* loaded = m_loadedResourceData.find(id);
    if (loaded != nullptr) {
        outData = *loaded;
        SDL_UnlockMutex(m_mutex);
        return true;
    }

    if (!m_resourceIndex.contains(id)) {
        SDL_UnlockMutex(m_mutex);
        return false;
    }

    uint8_t* state = m_resourceStates.find(id);
    uint8_t currentState = (state != nullptr) ? *state : RESOURCE_NOT_REQUESTED;

    if (currentState == RESOURCE_NOT_REQUESTED || currentState == RESOURCE_FAILED) {
        m_resourceStates.insert(id, RESOURCE_QUEUED);
        m_requestQueue.push_back(id);
        SDL_SignalCondition(m_requestCondition);
    }

    SDL_UnlockMutex(m_mutex);
    return false;
}

bool PakResource::areAllResourcesReady() {
    SDL_LockMutex(m_mutex);
    for (auto it = m_resourceIndex.begin(); it != m_resourceIndex.end(); ++it) {
        uint8_t* state = m_resourceStates.find(it.key());
        if (state == nullptr || *state != RESOURCE_READY) {
            SDL_UnlockMutex(m_mutex);
            return false;
        }
    }
    SDL_UnlockMutex(m_mutex);
    return true;
}

bool PakResource::hasResource(uint64_t id) {
    SDL_LockMutex(m_mutex);

    bool exists = m_resourceIndex.contains(id);
    SDL_UnlockMutex(m_mutex);
    return exists;
}

bool PakResource::tryGetAtlasUV(uint64_t textureId, AtlasUV& uv) {
    SDL_LockMutex(m_mutex);

    // Check cache first
    AtlasUV* cachedUV = m_atlasUVCache.find(textureId);
    if (cachedUV != nullptr) {
        uv = *cachedUV;
        SDL_UnlockMutex(m_mutex);
        return true;
    }

    SDL_UnlockMutex(m_mutex);

    ResourceData resData;
    if (!tryGetResource(textureId, resData)) {
        return false;
    }

    if (!resData.data || resData.size < sizeof(TextureHeader)) {
        return false;  // Not a texture or not found
    }

    // Check if this is a TextureHeader (atlas reference)
    // Use sizeof() for size comparison to stay in sync with structure definition
    if (resData.size == sizeof(TextureHeader)) {
        TextureHeader* texHeader = (TextureHeader*)resData.data;

        uv.atlasId = texHeader->atlasId;

        // UV coordinate layout in TextureHeader.coordinates[8]:
        //   [0,1] = bottom-left  (u0, v_bottom)
        //   [2,3] = bottom-right (u1, v_bottom)
        //   [4,5] = top-right    (u1, v_top)
        //   [6,7] = top-left     (u0, v_top)
        // Extract bounds: u0 from index 0, u1 from index 2
        //                 v_top from index 7, v_bottom from index 1
        uv.u0 = texHeader->coordinates[0];  // left u (from bottom-left)
        uv.u1 = texHeader->coordinates[2];  // right u (from bottom-right)
        uv.v0 = texHeader->coordinates[7];  // top v (from top-left)
        uv.v1 = texHeader->coordinates[1];  // bottom v (from bottom-left)

        // Initialize dimensions to 0 (will be set from atlas entry)
        uv.width = 0;
        uv.height = 0;

        // Get atlas to determine original dimensions
        ResourceData atlasData;
        if (tryGetResource(texHeader->atlasId, atlasData)) {
        if (atlasData.data && atlasData.size >= sizeof(AtlasHeader)) {
            AtlasHeader* atlasHeader = (AtlasHeader*)atlasData.data;
            AtlasEntry* entries = (AtlasEntry*)(atlasData.data + sizeof(AtlasHeader));

            // Find the entry for this texture
            for (uint16_t i = 0; i < atlasHeader->numEntries; i++) {
                if (entries[i].originalId == textureId) {
                    uv.width = entries[i].width;
                    uv.height = entries[i].height;
                    break;
                }
            }
        }
        }

        // Cache the result
        SDL_LockMutex(m_mutex);
        m_atlasUVCache.insert(textureId, uv);
        SDL_UnlockMutex(m_mutex);

        return true;
    }

    return false;  // Not an atlas reference (standalone image)
}

bool PakResource::tryGetAtlasData(uint64_t atlasId, ResourceData& outData) {
    outData = ResourceData{nullptr, 0, 0};

    ResourceData resData;
    if (!tryGetResource(atlasId, resData)) {
        return false;
    }

    if (!resData.data || resData.size < sizeof(AtlasHeader)) {
        return false;
    }

    outData = resData;
    return true;
}

bool PakResource::isResourceReady(uint64_t id) {
    SDL_LockMutex(m_mutex);
    uint8_t* state = m_resourceStates.find(id);
    bool ready = (state != nullptr && *state == RESOURCE_READY);
    SDL_UnlockMutex(m_mutex);
    return ready;
}