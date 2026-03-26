#pragma once

#include "../memory/MemoryAllocator.h"
#include <cstdint>

// Forward declaration
class SmallMemoryAllocator;

// Lightweight UTF-8 string class
// Uses SmallMemoryAllocator for memory management
// Optimized for performance via data-driven design
class String {
public:
    // Special value returned by find() when pattern not found
    static const uint64_t npos = (uint64_t)-1;
    // Constructors
    String(MemoryAllocator* allocator);
    String(const char* str, MemoryAllocator* allocator);
    String(const char* str, uint64_t length, MemoryAllocator* allocator);
    String(const String& other);
    String(String&& other) noexcept;

    // Destructor
    ~String();

    // Assignment operators
    String& operator=(const String& other);
    String& operator=(String&& other) noexcept;
    String& operator=(const char* str);

    // Comparison operators
    bool operator==(const String& other) const;
    bool operator==(const char* str) const;
    bool operator!=(const String& other) const;
    bool operator!=(const char* str) const;
    bool operator<(const String& other) const;
    bool operator>(const String& other) const;
    bool operator<=(const String& other) const;
    bool operator>=(const String& other) const;

    // Concatenation operators
    String operator+(const String& other) const;
    String operator+(const char* str) const;
    String& operator+=(const String& other);
    String& operator+=(const char* str);
    String& operator+=(char c);

    // Access operators
    char operator[](uint64_t index) const;
    char& operator[](uint64_t index);

    // String operations
    uint64_t length() const { return length_; }
    uint64_t capacity() const { return capacity_; }
    bool empty() const { return length_ == 0; }
    const char* c_str() const { return data_ ? data_ : ""; }
    const char* data() const { return data_; }
    char* data() { return data_; }

    // UTF-8 character count (may differ from byte length)
    uint64_t utf8Length() const;

    // Clear the string (keeps allocated memory)
    void clear();

    // Reserve capacity (does not shrink)
    void reserve(uint64_t newCapacity);

    // Resize string to new length (truncates or pads with null)
    void resize(uint64_t newLength);

    // Substring operations
    String substr(uint64_t pos, uint64_t len) const;

    // Find operations
    uint64_t find(const char* str, uint64_t pos = 0) const;
    uint64_t find(char c, uint64_t pos = 0) const;

    // Static utility functions
    static uint64_t SDL_strlen(const char* str);
    static int SDL_strcmp(const char* s1, const char* s2);
    static char* strcpy(char* dest, const char* src);
    static char* strncpy(char* dest, const char* src, uint64_t n);

    MemoryAllocator* allocator_;  // Memory allocator for string data
private:
    char* data_;       // Pointer to string data
    uint64_t length_;    // Length of string in bytes (excluding null terminator)
    uint64_t capacity_;  // Allocated capacity in bytes (excluding null terminator)

    // Get the global string allocator
    static SmallMemoryAllocator& getAllocator();

    // Ensure capacity is sufficient for at least minCapacity bytes
    void ensureCapacity(uint64_t minCapacity);

    // Calculate UTF-8 character length from byte sequence
    static int utf8CharLength(unsigned char c);
};

// Non-member concatenation operators
String operator+(const char* lhs, const String& rhs);
