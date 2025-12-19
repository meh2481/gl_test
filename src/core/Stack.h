#pragma once

#include "Vector.h"
#include "../memory/MemoryAllocator.h"
#include <cassert>

// Stack adapter using Vector as underlying container
// K = Element type
template<typename K>
class Stack {
public:
    // Constructor with custom allocator and allocation ID
    explicit Stack(MemoryAllocator& allocator, const char* callerId)
        : data_(allocator, callerId)
    {
    }

    ~Stack() {
        // Vector destructor handles cleanup
    }

    // Disable copy constructor and assignment
    Stack(const Stack&) = delete;
    Stack& operator=(const Stack&) = delete;

    // Push element onto stack
    void push(const K& value) {
        data_.push_back(value);
    }

    // Push element onto stack (move version)
    void push(K&& value) {
        data_.push_back(static_cast<K&&>(value));
    }

    // Remove top element from stack
    void pop() {
        assert(!empty() && "Stack::pop() called on empty stack");
        data_.pop_back();
    }

    // Get reference to top element
    K& top() {
        assert(!empty() && "Stack::top() called on empty stack");
        return data_.back();
    }

    // Get const reference to top element
    const K& top() const {
        assert(!empty() && "Stack::top() called on empty stack");
        return data_.back();
    }

    // Check if stack is empty
    bool empty() const {
        return data_.empty();
    }

    // Get number of elements in stack
    size_t size() const {
        return data_.size();
    }

    // Clear all elements
    void clear() {
        data_.clear();
    }

private:
    Vector<K> data_;
};
