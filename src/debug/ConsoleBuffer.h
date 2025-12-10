#pragma once

#ifdef DEBUG

#include <vector>
#include <mutex>
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
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.push_back(String(line.c_str(), stringAllocator_));
        // Keep buffer size reasonable (max 1000 lines)
        if (lines_.size() > 1000) {
            lines_.erase(lines_.begin());
        }
    }

    const std::vector<String>& getLines() {
        return lines_;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.clear();
    }

private:
    ConsoleBuffer() {
        stringAllocator_ = new SmallAllocator();
    }
    ~ConsoleBuffer() {
        if (stringAllocator_) {
            delete stringAllocator_;
        }
    }
    std::vector<String> lines_;
    std::mutex mutex_;
    MemoryAllocator* stringAllocator_;
};

// Custom streambuf to capture cout
class ConsoleCapture : public std::streambuf {
public:
    ConsoleCapture(std::ostream& stream, std::streambuf* oldBuf)
        : stream_(stream), oldBuf_(oldBuf), stringAllocator_(new SmallAllocator()), buffer_(stringAllocator_) {
    }

    ~ConsoleCapture() {
        // Restore original buffer
        stream_.rdbuf(oldBuf_);
        if (stringAllocator_) {
            delete stringAllocator_;
        }
    }

protected:
    virtual int_type overflow(int_type c) override {
        if (c != EOF) {
            if (c == '\n') {
                ConsoleBuffer::getInstance().addLine(buffer_);
                // Also write to original stream
                oldBuf_->sputn(buffer_.c_str(), buffer_.length());
                oldBuf_->sputc('\n');
                buffer_.clear();
            } else {
                buffer_ += static_cast<char>(c);
            }
        }
        return c;
    }

private:
    std::ostream& stream_;
    std::streambuf* oldBuf_;
    MemoryAllocator* stringAllocator_;
    String buffer_;
};

#endif // DEBUG
