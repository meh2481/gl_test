#pragma once

#include <SDL3/SDL.h>
#include <cassert>
#include "../core/String.h"
#include "../core/Vector.h"
#include "../memory/MemoryAllocator.h"

// SDL log categories
#define SDL_LOG_CATEGORY_APPLICATION SDL_LOG_CATEGORY_CUSTOM

// Console buffer to capture output for ImGui display and log via SDL
class ConsoleBuffer {
public:
    ConsoleBuffer(MemoryAllocator* allocator)
        : stringAllocator_(allocator)
        , lines_(*allocator, "ConsoleBuffer::lines_")
        , currentLine_(allocator) {
        assert(stringAllocator_ != nullptr);
        mutex_ = SDL_CreateMutex();
        assert(mutex_ != nullptr);
    }

    ~ConsoleBuffer() {
        // Clear lines before destroying (lines contain Strings that use the allocator)
        lines_.clear();
        if (mutex_) {
            SDL_DestroyMutex(mutex_);
            mutex_ = nullptr;
        }
        // Don't delete stringAllocator_ - we don't own it anymore
        stringAllocator_ = nullptr;
    }

    // Log a message with specified priority
    void log(SDL_LogPriority priority, const char* message) {
        assert(message != nullptr);
        // Call SDL_Log to output to system log
        SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, priority, "%s", message);
        // Store in buffer for ImGui display
        SDL_LockMutex(mutex_);
        lines_.push_back(String(message, stringAllocator_));
        if (lines_.size() > 1000) {
            lines_.erase(0);
        }
        SDL_UnlockMutex(mutex_);
    }

    // Variadic logging method for formatted messages
    template<typename... Args>
    void log(SDL_LogPriority priority, const char* format, Args... args) {
        char buffer[1024];
        SDL_snprintf(buffer, sizeof(buffer), format, args...);
        SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, priority, "%s", buffer);
        // Store in buffer for ImGui display
        SDL_LockMutex(mutex_);
        lines_.push_back(String(buffer, stringAllocator_));
        if (lines_.size() > 1000) {
            lines_.erase(0);
        }
        SDL_UnlockMutex(mutex_);
    }

    // Start building a log message with streaming
    ConsoleBuffer& operator<<(SDL_LogPriority priority) {
        currentPriority_ = priority;
        currentLine_.clear();
        return *this;
    }

    // Stream a C string
    ConsoleBuffer& operator<<(const char* str) {
        if (str) {
            currentLine_ += str;
        }
        return *this;
    }

    // Stream a String
    ConsoleBuffer& operator<<(const String& str) {
        currentLine_ += str;
        return *this;
    }

    // Stream an int
    ConsoleBuffer& operator<<(int value) {
        char buffer[32];
        SDL_snprintf(buffer, sizeof(buffer), "%d", value);
        currentLine_ += buffer;
        return *this;
    }

    // Stream a long
    ConsoleBuffer& operator<<(long value) {
        char buffer[32];
        SDL_snprintf(buffer, sizeof(buffer), "%ld", value);
        currentLine_ += buffer;
        return *this;
    }

    // Stream an unsigned int
    ConsoleBuffer& operator<<(unsigned int value) {
        char buffer[32];
        SDL_snprintf(buffer, sizeof(buffer), "%u", value);
        currentLine_ += buffer;
        return *this;
    }

    // Stream an unsigned long / size_t
    // Note: On some platforms size_t and unsigned long are the same type
    #if !defined(__x86_64__) || defined(_WIN32) || defined(_WIN64)
    ConsoleBuffer& operator<<(size_t value) {
        char buffer[32];
        SDL_snprintf(buffer, sizeof(buffer), "%zu", value);
        currentLine_ += buffer;
        return *this;
    }
    #else
    ConsoleBuffer& operator<<(unsigned long value) {
        char buffer[32];
        SDL_snprintf(buffer, sizeof(buffer), "%lu", value);
        currentLine_ += buffer;
        return *this;
    }
    #endif

    // Stream a float
    ConsoleBuffer& operator<<(float value) {
        char buffer[32];
        SDL_snprintf(buffer, sizeof(buffer), "%f", value);
        currentLine_ += buffer;
        return *this;
    }

    // Stream a double
    ConsoleBuffer& operator<<(double value) {
        char buffer[32];
        SDL_snprintf(buffer, sizeof(buffer), "%f", value);
        currentLine_ += buffer;
        return *this;
    }

    // Stream a bool
    ConsoleBuffer& operator<<(bool value) {
        currentLine_ += value ? "true" : "false";
        return *this;
    }

    // End of line marker type
    struct EndLine {};
    static EndLine endl;

    // Stream end of line - triggers actual logging
    ConsoleBuffer& operator<<(EndLine) {
        log(currentPriority_, currentLine_.c_str());
        currentLine_.clear();
        return *this;
    }

    const Vector<String>& getLines() {
        return lines_;
    }

    void clear() {
        SDL_LockMutex(mutex_);
        lines_.clear();
        SDL_UnlockMutex(mutex_);
    }

private:
    Vector<String> lines_;
    SDL_Mutex* mutex_;
    MemoryAllocator* stringAllocator_;
    String currentLine_;
    SDL_LogPriority currentPriority_ = SDL_LOG_PRIORITY_INFO;
};

// Define static endl
inline ConsoleBuffer::EndLine ConsoleBuffer::endl;
