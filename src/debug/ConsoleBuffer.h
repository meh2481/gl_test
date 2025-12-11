#pragma once

#ifdef DEBUG

#include <vector>
#include <SDL3/SDL.h>
#include <sstream>
#include <iostream>
#include <functional>
#include "../core/String.h"
#include "../memory/MemoryAllocator.h"
#include "../memory/SmallAllocator.h"

// Console buffer to capture output for ImGui display
class ConsoleBuffer {
public:
    static ConsoleBuffer& getInstance() {
        static ConsoleBuffer instance;
        return instance;
    }

    void addLine(const String& line) {
        SDL_LockMutex(mutex_);
        lines_.push_back(String(line.c_str(), stringAllocator_));
        if (lines_.size() > 1000) {
            lines_.erase(lines_.begin());
        }
        SDL_UnlockMutex(mutex_);
    }

    const std::vector<String>& getLines() {
        return lines_;
    }

    void clear() {
        SDL_LockMutex(mutex_);
        lines_.clear();
        SDL_UnlockMutex(mutex_);
    }

private:
    std::vector<String> lines_;
    SDL_Mutex* mutex_;
    MemoryAllocator* stringAllocator_;

    ConsoleBuffer() {
        stringAllocator_ = new SmallAllocator();
        mutex_ = SDL_CreateMutex();
    }
    ~ConsoleBuffer() {
        // Clear lines before deleting allocator (lines contain Strings that use the allocator)
        lines_.clear();
        if (stringAllocator_) {
            delete stringAllocator_;
            stringAllocator_ = nullptr;
        }
        if (mutex_) {
            SDL_DestroyMutex(mutex_);
            mutex_ = nullptr;
        }
    }
};

// Custom streambuf to capture cout
class ConsoleCapture : public std::streambuf {
public:
    ConsoleCapture(std::ostream& stream, std::streambuf* oldBuf)
        : stream_(stream), oldBuf_(oldBuf), stringAllocator_(new SmallAllocator()), buffer_(nullptr) {
        buffer_ = new String(stringAllocator_);
    }

    ~ConsoleCapture() {
        // Restore original buffer
        stream_.rdbuf(oldBuf_);
        // Explicitly delete buffer before allocator
        if (buffer_) {
            delete buffer_;
            buffer_ = nullptr;
        }
        if (stringAllocator_) {
            delete stringAllocator_;
            stringAllocator_ = nullptr;
        }
    }

protected:
    virtual int_type overflow(int_type c) override {
        if (c != EOF) {
            if (c == '\n') {
                ConsoleBuffer::getInstance().addLine(*buffer_);
                // Also write to original stream
                oldBuf_->sputn(buffer_->c_str(), buffer_->length());
                oldBuf_->sputc('\n');
                buffer_->clear();
            } else {
                *buffer_ += static_cast<char>(c);
            }
        }
        return c;
    }

private:
    std::ostream& stream_;
    std::streambuf* oldBuf_;
    MemoryAllocator* stringAllocator_;
    String* buffer_;
};

#endif // DEBUG
