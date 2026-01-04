#pragma once

#include "../memory/MemoryAllocator.h"
#include <cstddef>
#include <cstdint>

// Forward declaration
class SmallMemoryAllocator;

// Lightweight UTF-8 string class
// Uses SmallMemoryAllocator for memory management
// Optimized for performance via data-driven design
class String {
public:
    // Special value returned by find() when pattern not found
    static const size_t npos = (size_t)-1;
    // Constructors
    String(MemoryAllocator* allocator);
    String(const char* str, MemoryAllocator* allocator);
    String(const char* str, size_t length, MemoryAllocator* allocator);
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
    char operator[](size_t index) const;
    char& operator[](size_t index);

    // String operations
    size_t length() const { return length_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return length_ == 0; }
    const char* c_str() const { return data_ ? data_ : ""; }
    const char* data() const { return data_; }
    char* data() { return data_; }

    // UTF-8 character count (may differ from byte length)
    size_t utf8Length() const;

    // Clear the string (keeps allocated memory)
    void clear();

    // Reserve capacity (does not shrink)
    void reserve(size_t newCapacity);

    // Resize string to new length (truncates or pads with null)
    void resize(size_t newLength);

    // Substring operations
    String substr(size_t pos, size_t len) const;

    // Find operations
    size_t find(const char* str, size_t pos = 0) const;
    size_t find(char c, size_t pos = 0) const;

    // Static utility functions
    static size_t strlen(const char* str);
    static int strcmp(const char* s1, const char* s2);
    static char* strcpy(char* dest, const char* src);
    static char* strncpy(char* dest, const char* src, size_t n);

    MemoryAllocator* allocator_;  // Memory allocator for string data
private:
    char* data_;       // Pointer to string data
    size_t length_;    // Length of string in bytes (excluding null terminator)
    size_t capacity_;  // Allocated capacity in bytes (excluding null terminator)

    // Get the global string allocator
    static SmallMemoryAllocator& getAllocator();

    // Ensure capacity is sufficient for at least minCapacity bytes
    void ensureCapacity(size_t minCapacity);

    // Calculate UTF-8 character length from byte sequence
    static int utf8CharLength(unsigned char c);
};

// Non-member concatenation operators
String operator+(const char* lhs, const String& rhs);
