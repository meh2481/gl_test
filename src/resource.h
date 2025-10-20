#include <cstddef>

struct ShaderCode {
    char* data;
    size_t size;
};

ShaderCode readFile(const char* filename);
void freeShaderCode(ShaderCode& code);