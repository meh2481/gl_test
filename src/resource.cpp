#include "resource.h"
#include "ResourceTypes.h"
#include <cstring>
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

ResourceData PakResource::getResource(uint64_t id) {
    SDL_LockMutex(m_mutex);
    
    if (!m_pakData.data) {
        SDL_UnlockMutex(m_mutex);
        return ResourceData{nullptr, 0};
    }
    
    PakFileHeader* header = (PakFileHeader*)m_pakData.data;
    if (memcmp(header->sig, "PAKC", 4) != 0) {
        SDL_UnlockMutex(m_mutex);
        return ResourceData{nullptr, 0};
    }
    
    ResourcePtr* ptrs = (ResourcePtr*)(m_pakData.data + sizeof(PakFileHeader));
    for (uint32_t i = 0; i < header->numResources; i++) {
        if (ptrs[i].id == id) {
            CompressionHeader* comp = (CompressionHeader*)(m_pakData.data + ptrs[i].offset);
            char* compressedData = (char*)(comp + 1);
            if (comp->compressionType == COMPRESSION_FLAGS_UNCOMPRESSED) {
                ResourceData result = ResourceData{compressedData, comp->decompressedSize};
                SDL_UnlockMutex(m_mutex);
                return result;
            } else if (comp->compressionType == COMPRESSION_FLAGS_LZ4) {
                // Check if already decompressed
                if (m_decompressedData.find(id) == m_decompressedData.end()) {
                    m_decompressedData[id].resize(comp->decompressedSize);
                    int result = LZ4_decompress_safe(compressedData, m_decompressedData[id].data(), comp->compressedSize, comp->decompressedSize);
                    if (result != (int)comp->decompressedSize) {
                        SDL_UnlockMutex(m_mutex);
                        return ResourceData{nullptr, 0};
                    }
                }
                ResourceData result = ResourceData{(char*)m_decompressedData[id].data(), comp->decompressedSize};
                SDL_UnlockMutex(m_mutex);
                return result;
            }
        }
    }
    
    SDL_UnlockMutex(m_mutex);
    return ResourceData{nullptr, 0};
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