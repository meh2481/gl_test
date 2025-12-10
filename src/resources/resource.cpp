#include "resource.h"
#include "../core/ResourceTypes.h"
#include <cstring>
#include <cassert>
#include <iostream>
#include <lz4.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

PakResource::PakResource() : m_pakData{nullptr, 0}
#ifdef _WIN32
, m_hFile(INVALID_HANDLE_VALUE), m_hMapping(NULL)
#else
, m_fd(-1)
#endif
{
    m_mutex = SDL_CreateMutex();
    assert(m_mutex != nullptr);
}

PakResource::~PakResource() {
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
    size_t size = sb.st_size;
    void* addr = mmap(NULL, size, PROT_READ, MAP_PRIVATE, m_fd, 0);
    if (addr == MAP_FAILED) {
        close(m_fd);
        m_fd = -1;
        return false;
    }
    m_pakData = ResourceData{(char*)addr, size};
#endif
    return true;
}

bool PakResource::reload(const char* filename) {
    // Unmap current
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
    // Clear caches
    m_decompressedData.clear();
    m_atlasUVCache.clear();
    // Load again
    return load(filename);
}

ResourceData PakResource::getResource(uint64_t id) {
    SDL_LockMutex(m_mutex);

    if (!m_pakData.data) {
        std::cerr << "Resource pak not loaded, cannot get resource id " << id << std::endl;
        SDL_UnlockMutex(m_mutex);
        assert(false);
        return ResourceData{nullptr, 0, 0};
    }

    PakFileHeader* header = (PakFileHeader*)m_pakData.data;
    if (memcmp(header->sig, "PAKC", 4) != 0) {
        std::cerr << "Invalid pak file signature" << std::endl;
        SDL_UnlockMutex(m_mutex);
        assert(false);
        return ResourceData{nullptr, 0, 0};
    }

    ResourcePtr* ptrs = (ResourcePtr*)(m_pakData.data + sizeof(PakFileHeader));
    for (uint32_t i = 0; i < header->numResources; i++) {
        if (ptrs[i].id == id) {
            CompressionHeader* comp = (CompressionHeader*)(m_pakData.data + ptrs[i].offset);
            char* compressedData = (char*)(comp + 1);
            if (comp->compressionType == COMPRESSION_FLAGS_UNCOMPRESSED) {
                ResourceData result = ResourceData{compressedData, comp->decompressedSize, comp->type};
                SDL_UnlockMutex(m_mutex);
                return result;
            } else if (comp->compressionType == COMPRESSION_FLAGS_LZ4) {
                // Check if already decompressed (cache hit)
                if (m_decompressedData.find(id) != m_decompressedData.end()) {
                    std::cout << "Resource " << id << ": cache hit (" << comp->decompressedSize << " bytes)" << std::endl;
                    ResourceData result = ResourceData{(char*)m_decompressedData[id].data(), comp->decompressedSize, comp->type};
                    SDL_UnlockMutex(m_mutex);
                    return result;
                }
                // Cache miss - decompress
                std::cout << "Resource " << id << ": cache miss, decompressing " << comp->compressedSize << " -> " << comp->decompressedSize << " bytes" << std::endl;
                m_decompressedData[id].resize(comp->decompressedSize);
                int result = LZ4_decompress_safe(compressedData, m_decompressedData[id].data(), comp->compressedSize, comp->decompressedSize);
                if (result != (int)comp->decompressedSize) {
                    std::cerr << "LZ4 decompression failed for resource " << id << std::endl;
                    SDL_UnlockMutex(m_mutex);
                    assert(false);
                    return ResourceData{nullptr, 0, 0};
                }
                ResourceData resData = ResourceData{(char*)m_decompressedData[id].data(), comp->decompressedSize, comp->type};
                SDL_UnlockMutex(m_mutex);
                return resData;
            }
        }
    }

    std::cerr << "Resource " << id << " not found in pak" << std::endl;
    SDL_UnlockMutex(m_mutex);
    assert(false);
    return ResourceData{nullptr, 0, 0};
}

bool PakResource::getAtlasUV(uint64_t textureId, AtlasUV& uv) {
    SDL_LockMutex(m_mutex);

    // Check cache first
    auto cacheIt = m_atlasUVCache.find(textureId);
    if (cacheIt != m_atlasUVCache.end()) {
        uv = cacheIt->second;
        SDL_UnlockMutex(m_mutex);
        return true;
    }

    SDL_UnlockMutex(m_mutex);

    // Get the resource data
    ResourceData resData = getResource(textureId);
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
        ResourceData atlasData = getResource(texHeader->atlasId);
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

        // Cache the result
        SDL_LockMutex(m_mutex);
        m_atlasUVCache[textureId] = uv;
        SDL_UnlockMutex(m_mutex);

        return true;
    }

    return false;  // Not an atlas reference (standalone image)
}

ResourceData PakResource::getAtlasData(uint64_t atlasId) {
    // Get the atlas resource
    ResourceData resData = getResource(atlasId);
    if (!resData.data || resData.size < sizeof(AtlasHeader)) {
        return ResourceData{nullptr, 0, 0};
    }

    // The atlas data contains: AtlasHeader + AtlasEntry[] + compressed image data
    AtlasHeader* header = (AtlasHeader*)resData.data;
    size_t entriesSize = sizeof(AtlasEntry) * header->numEntries;
    size_t imageOffset = sizeof(AtlasHeader) + entriesSize;

    // Return the image portion with header information
    // We return the entire atlas data so the renderer can parse it
    return resData;
}

// Structure for async preload thread
struct PreloadData {
    PakResource* resource;
    uint64_t id;
};

static int preloadThread(void* data) {
    PreloadData* preloadData = (PreloadData*)data;
    // Just call getResource which will decompress if needed
    preloadData->resource->getResource(preloadData->id);
    delete preloadData;
    return 0;
}

void PakResource::preloadResourceAsync(uint64_t id) {
    PreloadData* data = new PreloadData{this, id};
    SDL_Thread* thread = SDL_CreateThread(preloadThread, "ResourcePreload", data);
    if (thread) {
        SDL_DetachThread(thread);
    } else {
        delete data;
    }
}

bool PakResource::isResourceReady(uint64_t id) {
    SDL_LockMutex(m_mutex);
    bool ready = m_decompressedData.find(id) != m_decompressedData.end();
    SDL_UnlockMutex(m_mutex);
    return ready;
}