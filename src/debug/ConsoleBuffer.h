#pragma once

#ifdef DEBUG

#include <string>
#include <vector>
#include <mutex>
#include <sstream>
#include <iostream>

// Console buffer to capture output for ImGui display
class ConsoleBuffer {
public:
    static ConsoleBuffer& getInstance() {
        static ConsoleBuffer instance;
        return instance;
    }

    void addLine(const std::string& line) {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.push_back(line);
        // Keep buffer size reasonable (max 1000 lines)
        if (lines_.size() > 1000) {
            lines_.erase(lines_.begin());
        }
    }

    void getLines(std::vector<std::string>& outLines) {
        std::lock_guard<std::mutex> lock(mutex_);
        outLines = lines_;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.clear();
    }

private:
    ConsoleBuffer() {}
    std::vector<std::string> lines_;
    std::mutex mutex_;
};

// Custom streambuf to capture cout
class ConsoleCapture : public std::streambuf {
public:
    ConsoleCapture(std::ostream& stream, std::streambuf* oldBuf)
        : stream_(stream), oldBuf_(oldBuf) {
    }

    ~ConsoleCapture() {
        // Restore original buffer
        stream_.rdbuf(oldBuf_);
    }

protected:
    virtual int_type overflow(int_type c) override {
        if (c != EOF) {
            if (c == '\n') {
                ConsoleBuffer::getInstance().addLine(buffer_);
                // Also write to original stream
                oldBuf_->sputn(buffer_.c_str(), buffer_.size());
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
    std::string buffer_;
};

#endif // DEBUG
