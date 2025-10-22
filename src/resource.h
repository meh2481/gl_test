#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <map>

struct ResourceData {
    char* data;
    size_t size;
};

class PakResource {
public:
    PakResource();
    ~PakResource();
    bool load(const char* filename);
    ResourceData getResource(uint64_t id);
private:
    ResourceData m_pakData;
    std::map<uint64_t, std::vector<char>> m_decompressedData;
#ifdef _WIN32
    HANDLE m_hFile;
    HANDLE m_hMapping;
#else
    int m_fd;
#endif
};