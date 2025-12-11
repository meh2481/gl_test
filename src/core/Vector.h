#pragma once

#include "../memory/MemoryAllocator.h"
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <new>

template<typename T>
class Vector {
public:
    explicit Vector(MemoryAllocator& allocator)
        : data_(nullptr)
        , size_(0)
        , capacity_(0)
        , allocator_(&allocator) {
    }

    ~Vector() {
        clear();
        if (data_ && allocator_) {
            allocator_->free(data_);
            data_ = nullptr;
        }
        capacity_ = 0;
    }

    Vector(const Vector& other)
        : data_(nullptr)
        , size_(0)
        , capacity_(0)
        , allocator_(nullptr) {
        allocator_ = other.allocator_;
        reserve(other.size_);
        for (size_t i = 0; i < other.size_; ++i) {
            new (&data_[i]) T(other.data_[i]);
        }
        size_ = other.size_;
    }

    Vector& operator=(const Vector& other) {
        if (this != &other) {
            clear();
            if (data_) {
                allocator_->free(data_);
                data_ = nullptr;
                capacity_ = 0;
            }
            allocator_ = other.allocator_;
            reserve(other.size_);
            for (size_t i = 0; i < other.size_; ++i) {
                new (&data_[i]) T(other.data_[i]);
            }
            size_ = other.size_;
        }
        return *this;
    }

    Vector(Vector&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
        , allocator_(other.allocator_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        other.allocator_ = nullptr;
    }

    Vector& operator=(Vector&& other) noexcept {
        if (this != &other) {
            clear();
            if (data_) {
                allocator_->free(data_);
            }
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            allocator_ = other.allocator_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
            other.allocator_ = nullptr;
        }
        return *this;
    }

    void push_back(const T& value) {
        if (size_ >= capacity_) {
            grow();
        }
        new (&data_[size_]) T(value);
        ++size_;
    }

    void push_back(T&& value) {
        if (size_ >= capacity_) {
            grow();
        }
        new (&data_[size_]) T(static_cast<T&&>(value));
        ++size_;
    }

    void pop_back() {
        assert(size_ > 0);
        --size_;
        data_[size_].~T();
    }

    T& operator[](size_t index) {
        assert(index < size_);
        return data_[index];
    }

    const T& operator[](size_t index) const {
        assert(index < size_);
        return data_[index];
    }

    T& at(size_t index) {
        assert(index < size_);
        return data_[index];
    }

    const T& at(size_t index) const {
        assert(index < size_);
        return data_[index];
    }

    T& front() {
        assert(size_ > 0);
        return data_[0];
    }

    const T& front() const {
        assert(size_ > 0);
        return data_[0];
    }

    T& back() {
        assert(size_ > 0);
        return data_[size_ - 1];
    }

    const T& back() const {
        assert(size_ > 0);
        return data_[size_ - 1];
    }

    T* data() {
        return data_;
    }

    const T* data() const {
        return data_;
    }

    size_t size() const {
        return size_;
    }

    size_t capacity() const {
        return capacity_;
    }

    bool empty() const {
        return size_ == 0;
    }

    void clear() {
        for (size_t i = 0; i < size_; ++i) {
            data_[i].~T();
        }
        size_ = 0;
    }

    void reserve(size_t newCapacity) {
        if (newCapacity <= capacity_) {
            return;
        }

        T* newData = static_cast<T*>(allocator_->allocate(newCapacity * sizeof(T)));
        assert(newData != nullptr || newCapacity == 0);
        for (size_t i = 0; i < size_; ++i) {
            new (&newData[i]) T(static_cast<T&&>(data_[i]));
            data_[i].~T();
        }

        if (data_) {
            allocator_->free(data_);
        }

        data_ = newData;
        capacity_ = newCapacity;
    }

    void resize(size_t newSize) {
        if (newSize > capacity_) {
            reserve(newSize);
        }

        if (newSize > size_) {
            for (size_t i = size_; i < newSize; ++i) {
                new (&data_[i]) T();
            }
        } else if (newSize < size_) {
            for (size_t i = newSize; i < size_; ++i) {
                data_[i].~T();
            }
        }

        size_ = newSize;
    }

    void resize(size_t newSize, const T& value) {
        if (newSize > capacity_) {
            reserve(newSize);
        }

        if (newSize > size_) {
            for (size_t i = size_; i < newSize; ++i) {
                new (&data_[i]) T(value);
            }
        } else if (newSize < size_) {
            for (size_t i = newSize; i < size_; ++i) {
                data_[i].~T();
            }
        }

        size_ = newSize;
    }

    void shrink_to_fit() {
        if (size_ < capacity_) {
            if (size_ == 0) {
                if (data_) {
                    allocator_->free(data_);
                    data_ = nullptr;
                }
                capacity_ = 0;
            } else {
                T* newData = static_cast<T*>(allocator_->allocate(size_ * sizeof(T)));
                assert(newData != nullptr);
                for (size_t i = 0; i < size_; ++i) {
                    new (&newData[i]) T(static_cast<T&&>(data_[i]));
                    data_[i].~T();
                }

                if (data_) {
                    allocator_->free(data_);
                }

                data_ = newData;
                capacity_ = size_;
            }
        }
    }

    void erase(size_t index) {
        assert(index < size_);
        assert(size_ > 0);

        data_[index].~T();

        for (size_t i = index + 1; i < size_; ++i) {
            new (&data_[i - 1]) T(static_cast<T&&>(data_[i]));
            data_[i].~T();
        }

        --size_;
    }

    void insert(size_t index, const T& value) {
        assert(index <= size_);

        if (size_ >= capacity_) {
            grow();
        }

        if (index < size_) {
            new (&data_[size_]) T(static_cast<T&&>(data_[size_ - 1]));
            if (size_ > 1) {
                for (size_t i = size_ - 1; i > index; --i) {
                    data_[i] = static_cast<T&&>(data_[i - 1]);
                }
            }
            data_[index] = value;
        } else {
            new (&data_[index]) T(value);
        }

        ++size_;
    }

    T* begin() {
        return data_;
    }

    const T* begin() const {
        return data_;
    }

    T* end() {
        return data_ + size_;
    }

    const T* end() const {
        return data_ + size_;
    }

    // Get the allocator used by this vector
    MemoryAllocator& getAllocator() const {
        return *allocator_;
    }

private:
    void grow() {
        size_t newCapacity = capacity_ == 0 ? 8 : capacity_ * 2;
        reserve(newCapacity);
    }

    T* data_;
    size_t size_;
    size_t capacity_;
    MemoryAllocator* allocator_;
};
