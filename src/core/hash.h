#pragma once

#include <cstdint>
#include <string>
#include <algorithm>

// Simple hash function for C-strings (FNV-1a hash)
static uint64_t hashCString(const char* str) {
    // uint64_t hash = 14695981039346656037ULL;
    // while (*str) {
    //     hash ^= (uint64_t)*str++;
    //     hash *= 1099511628211ULL;
    // }
    // return hash;
    uint64_t hash = std::hash<std::string>{}(std::string(str));
    return hash;
}
