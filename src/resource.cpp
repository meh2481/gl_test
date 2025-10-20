#include "resource.h"

#ifdef _WIN32
#include <windows.h>

ShaderCode readFile(const char* filename) {
    HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        // TODO: error handling
        return ShaderCode{nullptr, 0};
    }
    DWORD size = GetFileSize(hFile, NULL);
    HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMapping) {
        CloseHandle(hFile);
        return ShaderCode{nullptr, 0};
    }
    void* addr = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
    if (!addr) {
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return ShaderCode{nullptr, 0};
    }
    CloseHandle(hMapping);
    CloseHandle(hFile);
    return ShaderCode{(char*)addr, size};
}

void freeShaderCode(ShaderCode& code) {
    if (code.data) {
        UnmapViewOfFile(code.data);
        code.data = nullptr;
        code.size = 0;
    }
}

#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

ShaderCode readFile(const char* filename) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        return ShaderCode{nullptr, 0};
    }
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        close(fd);
        return ShaderCode{nullptr, 0};
    }
    size_t size = sb.st_size;
    void* addr = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) {
        close(fd);
        return ShaderCode{nullptr, 0};
    }
    close(fd);
    return ShaderCode{(char*)addr, size};
}

void freeShaderCode(ShaderCode& code) {
    if (code.data) {
        munmap(code.data, code.size);
        code.data = nullptr;
        code.size = 0;
    }
}

#endif