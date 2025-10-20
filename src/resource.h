#include <cstddef>
#include <cstdint>
#include <vector>

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
    std::vector<char> m_decompressedData;
#ifdef _WIN32
    HANDLE m_hFile;
    HANDLE m_hMapping;
#else
    int m_fd;
#endif
};