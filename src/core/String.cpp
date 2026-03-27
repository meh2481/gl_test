#include "String.h"
#include <cassert>
#include <SDL3/SDL.h>

// Default growth factor for capacity
static const Uint64 GROWTH_FACTOR = 2;
static const Uint64 MIN_CAPACITY = 16;

// Default constructor
String::String(MemoryAllocator* allocator) : data_(nullptr), length_(0), capacity_(0), allocator_(allocator) {
}

// Constructor from C-string
String::String(const char* str, MemoryAllocator* allocator) : data_(nullptr), length_(0), capacity_(0), allocator_(allocator) {
    if (str) {
        length_ = SDL_strlen(str);
        if (length_ > 0) {
            ensureCapacity(length_);
            strcpy(data_, str);
        }
    }
}

// Constructor from C-string with explicit length
String::String(const char* str, Uint64 length, MemoryAllocator* allocator) : data_(nullptr), length_(0), capacity_(0), allocator_(allocator) {
    if (str && length > 0) {
        length_ = length;
        ensureCapacity(length_);
        strncpy(data_, str, length_);
        data_[length_] = '\0';
    }
}

// Copy constructor
String::String(const String& other) : data_(nullptr), length_(0), capacity_(0), allocator_(other.allocator_) {
    if (other.length_ > 0) {
        length_ = other.length_;
        ensureCapacity(length_);
        strcpy(data_, other.data_);
    }
}

// Move constructor
String::String(String&& other) noexcept
    : data_(other.data_), length_(other.length_), capacity_(other.capacity_), allocator_(other.allocator_) {
    other.data_ = nullptr;
    other.length_ = 0;
    other.capacity_ = 0;
}

// Destructor
String::~String() {
    if (data_) {
        allocator_->free(data_);
        data_ = nullptr;
    }
    length_ = 0;
    capacity_ = 0;
}

// Copy assignment operator
String& String::operator=(const String& other) {
    if (this != &other) {
        clear();
        this->allocator_ = other.allocator_;
        if (other.length_ > 0) {
            length_ = other.length_;
            ensureCapacity(length_);
            strcpy(data_, other.data_);
        }
    }
    return *this;
}

// Move assignment operator
String& String::operator=(String&& other) noexcept {
    if (this != &other) {
        // Free existing data
        if (data_) {
            allocator_->free(data_);
        }
        // Move data from other
        data_ = other.data_;
        length_ = other.length_;
        capacity_ = other.capacity_;
        allocator_ = other.allocator_;
        // Null out other
        other.data_ = nullptr;
        other.length_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

// Assignment from C-string
String& String::operator=(const char* str) {
    clear();
    if (str) {
        length_ = SDL_strlen(str);
        if (length_ > 0) {
            ensureCapacity(length_);
            strcpy(data_, str);
        }
    }
    return *this;
}

// Equality comparison with String
bool String::operator==(const String& other) const {
    if (length_ != other.length_) return false;
    if (length_ == 0) return true;
    return SDL_strcmp(data_, other.data_) == 0;
}

// Equality comparison with C-string
bool String::operator==(const char* str) const {
    if (!str) return length_ == 0;
    if (length_ == 0) return SDL_strlen(str) == 0;
    return SDL_strcmp(data_, str) == 0;
}

// Inequality comparison with String
bool String::operator!=(const String& other) const {
    return !(*this == other);
}

// Inequality comparison with C-string
bool String::operator!=(const char* str) const {
    return !(*this == str);
}

// Less than comparison
bool String::operator<(const String& other) const {
    if (length_ == 0 && other.length_ == 0) return false;
    if (length_ == 0) return true;
    if (other.length_ == 0) return false;
    return SDL_strcmp(data_, other.data_) < 0;
}

// Greater than comparison
bool String::operator>(const String& other) const {
    return other < *this;
}

// Less than or equal comparison
bool String::operator<=(const String& other) const {
    return !(other < *this);
}

// Greater than or equal comparison
bool String::operator>=(const String& other) const {
    return !(*this < other);
}

// Concatenation with String
String String::operator+(const String& other) const {
    String result(allocator_);
    Uint64 newLength = length_ + other.length_;
    if (newLength > 0) {
        result.ensureCapacity(newLength);
        result.length_ = newLength;
        if (length_ > 0) {
            strcpy(result.data_, data_);
        }
        if (other.length_ > 0) {
            strcpy(result.data_ + length_, other.data_);
        }
    }
    return result;
}

// Concatenation with C-string
String String::operator+(const char* str) const {
    String result(allocator_);
    if (!str) {
        return *this;
    }
    Uint64 strLen = SDL_strlen(str);
    if (strLen == 0) {
        return *this;
    }
    Uint64 newLength = length_ + strLen;
    result.ensureCapacity(newLength);
    result.length_ = newLength;
    if (length_ > 0) {
        strcpy(result.data_, data_);
    }
    strcpy(result.data_ + length_, str);
    return result;
}

// Append String
String& String::operator+=(const String& other) {
    if (other.length_ > 0) {
        Uint64 newLength = length_ + other.length_;
        ensureCapacity(newLength);
        strcpy(data_ + length_, other.data_);
        length_ = newLength;
    }
    return *this;
}

// Append C-string
String& String::operator+=(const char* str) {
    if (str) {
        Uint64 strLen = SDL_strlen(str);
        if (strLen > 0) {
            Uint64 newLength = length_ + strLen;
            ensureCapacity(newLength);
            strcpy(data_ + length_, str);
            length_ = newLength;
        }
    }
    return *this;
}

// Append character
String& String::operator+=(char c) {
    ensureCapacity(length_ + 1);
    data_[length_] = c;
    length_++;
    data_[length_] = '\0';
    return *this;
}

// Access operator (const)
char String::operator[](Uint64 index) const {
    assert(index < length_);
    return data_[index];
}

// Access operator (non-const)
char& String::operator[](Uint64 index) {
    assert(index < length_);
    return data_[index];
}

// UTF-8 character count
Uint64 String::utf8Length() const {
    if (length_ == 0) return 0;
    Uint64 count = 0;
    Uint64 i = 0;
    while (i < length_) {
        int charLen = utf8CharLength((unsigned char)data_[i]);
        assert(charLen > 0 && i + charLen <= length_);
        i += charLen;
        count++;
    }
    return count;
}

// Clear the string
void String::clear() {
    length_ = 0;
    if (data_) {
        data_[0] = '\0';
    }
}

// Reserve capacity
void String::reserve(Uint64 newCapacity) {
    if (newCapacity > capacity_) {
        char* newData = (char*)allocator_->allocate(newCapacity + 1, "String.cpp:265");
        assert(newData != nullptr);
        if (data_ && length_ > 0) {
            strcpy(newData, data_);
        } else {
            newData[0] = '\0';
        }
        if (data_) {
            allocator_->free(data_);
            data_ = nullptr;
        }
        data_ = newData;
        capacity_ = newCapacity;
    }
}

// Resize string
void String::resize(Uint64 newLength) {
    if (newLength > length_) {
        ensureCapacity(newLength);
        for (Uint64 i = length_; i < newLength; i++) {
            data_[i] = '\0';
        }
    }
    length_ = newLength;
    if (data_) {
        data_[length_] = '\0';
    }
}

// Substring
String String::substr(Uint64 pos, Uint64 len) const {
    assert(pos <= length_);
    Uint64 actualLen = len;
    if (pos + actualLen > length_) {
        actualLen = length_ - pos;
    }
    if (actualLen == 0) {
        return String(allocator_);
    }
    return String(data_ + pos, actualLen, allocator_);
}

// Find C-string
Uint64 String::find(const char* str, Uint64 pos) const {
    if (!str || pos >= length_) {
        return npos;
    }
    Uint64 strLen = SDL_strlen(str);
    if (strLen == 0 || strLen > length_ - pos) {
        return npos;
    }
    for (Uint64 i = pos; i <= length_ - strLen; i++) {
        bool found = true;
        for (Uint64 j = 0; j < strLen; j++) {
            if (data_[i + j] != str[j]) {
                found = false;
                break;
            }
        }
        if (found) {
            return i;
        }
    }
    return npos;
}

// Find character
Uint64 String::find(char c, Uint64 pos) const {
    if (pos >= length_) {
        return npos;
    }
    for (Uint64 i = pos; i < length_; i++) {
        if (data_[i] == c) {
            return i;
        }
    }
    return npos;
}

// Static SDL_strlen
Uint64 String::SDL_strlen(const char* str) {
    if (!str) return 0;
    Uint64 len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

// Static SDL_strcmp
int String::SDL_strcmp(const char* s1, const char* s2) {
    assert(s1 != nullptr && s2 != nullptr);
    Uint64 i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] < s2[i]) return -1;
        if (s1[i] > s2[i]) return 1;
        i++;
    }
    if (s1[i] == '\0' && s2[i] == '\0') return 0;
    if (s1[i] == '\0') return -1;
    return 1;
}

// Static strcpy
char* String::strcpy(char* dest, const char* src) {
    assert(dest != nullptr && src != nullptr);
    Uint64 i = 0;
    while (src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
    return dest;
}

// Static strncpy
char* String::strncpy(char* dest, const char* src, Uint64 n) {
    assert(dest != nullptr && src != nullptr);
    Uint64 i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

// Ensure capacity is sufficient
void String::ensureCapacity(Uint64 minCapacity) {
    if (minCapacity <= capacity_) {
        return;
    }
    Uint64 newCapacity = capacity_;
    if (newCapacity < MIN_CAPACITY) {
        newCapacity = MIN_CAPACITY;
    }
    while (newCapacity < minCapacity) {
        newCapacity *= GROWTH_FACTOR;
    }
    reserve(newCapacity);
}

// Calculate UTF-8 character length from first byte
int String::utf8CharLength(unsigned char c) {
    if ((c & 0x80) == 0) return 1;       // 0xxxxxxx - 1 byte (ASCII)
    if ((c & 0xE0) == 0xC0) return 2;    // 110xxxxx - 2 bytes
    if ((c & 0xF0) == 0xE0) return 3;    // 1110xxxx - 3 bytes
    if ((c & 0xF8) == 0xF0) return 4;    // 11110xxx - 4 bytes
    // Invalid UTF-8 byte - assert without logging
    assert(false);
    return 1;
}

// Non-member concatenation operator
String operator+(const char* lhs, const String& rhs) {
    String result(lhs, rhs.allocator_);
    result += rhs;
    return result;
}
