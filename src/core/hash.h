#pragma once


// Simple hash function for C-strings (FNV-1a hash)
static Uint64 hashCString(const char* str) {
    Uint64 hash = 14695981039346656037ULL;
    while (*str) {
        hash ^= (Uint64)*str++;
        hash *= 1099511628211ULL;
    }
    return hash;
}
