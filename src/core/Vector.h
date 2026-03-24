#pragma once

#include "../memory/MemoryAllocator.h"
#include <cstdint>
#include <cassert>
#include <new>

template<typename T>
class Vector {
public:
    explicit Vector(MemoryAllocator& allocator, const char* callerId)
        : data_(nullptr)
        , size_(0)
        , capacity_(0)
        , allocator_(&allocator)
        , callerId_(callerId) {
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
        , allocator_(nullptr)
        , callerId_(other.callerId_) {
        allocator_ = other.allocator_;
        reserve(other.size_);
        for (uint64_t i = 0; i < other.size_; ++i) {
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
            callerId_ = other.callerId_;
            reserve(other.size_);
            for (uint64_t i = 0; i < other.size_; ++i) {
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
        , allocator_(other.allocator_)
        , callerId_(other.callerId_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        other.allocator_ = nullptr;
        other.callerId_ = nullptr;
    }

    Vector& operator=(Vector&& other) noexcept {
        if (this != &other) {
            clear();
            if (data_ && allocator_) {
                allocator_->free(data_);
            }
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            allocator_ = other.allocator_;
            callerId_ = other.callerId_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
            other.allocator_ = nullptr;
            other.callerId_ = nullptr;
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

    T& operator[](uint64_t index) {
        assert(index < size_);
        return data_[index];
    }

    const T& operator[](uint64_t index) const {
        assert(index < size_);
        return data_[index];
    }

    T& at(uint64_t index) {
        assert(index < size_);
        return data_[index];
    }

    const T& at(uint64_t index) const {
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

    uint64_t size() const {
        return size_;
    }

    uint64_t capacity() const {
        return capacity_;
    }

    bool empty() const {
        return size_ == 0;
    }

    void clear() {
        for (uint64_t i = 0; i < size_; ++i) {
            data_[i].~T();
        }
        size_ = 0;
    }

    void reserve(uint64_t newCapacity) {
        if (newCapacity <= capacity_) {
            return;
        }

        T* newData = static_cast<T*>(allocator_->allocate(newCapacity * sizeof(T), callerId_));
        assert(newData != nullptr || newCapacity == 0);
        for (uint64_t i = 0; i < size_; ++i) {
            new (&newData[i]) T(static_cast<T&&>(data_[i]));
            data_[i].~T();
        }

        if (data_) {
            allocator_->free(data_);
        }

        data_ = newData;
        capacity_ = newCapacity;
    }

    void resize(uint64_t newSize) {
        if (newSize > capacity_) {
            reserve(newSize);
        }

        if (newSize > size_) {
            for (uint64_t i = size_; i < newSize; ++i) {
                new (&data_[i]) T();
            }
        } else if (newSize < size_) {
            for (uint64_t i = newSize; i < size_; ++i) {
                data_[i].~T();
            }
        }

        size_ = newSize;
    }

    void resize(uint64_t newSize, const T& value) {
        if (newSize > capacity_) {
            reserve(newSize);
        }

        if (newSize > size_) {
            for (uint64_t i = size_; i < newSize; ++i) {
                new (&data_[i]) T(value);
            }
        } else if (newSize < size_) {
            for (uint64_t i = newSize; i < size_; ++i) {
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
                T* newData = static_cast<T*>(allocator_->allocate(size_ * sizeof(T), callerId_));
                assert(newData != nullptr);
                for (uint64_t i = 0; i < size_; ++i) {
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

    void erase(uint64_t index) {
        assert(index < size_);
        assert(size_ > 0);

        data_[index].~T();

        for (uint64_t i = index + 1; i < size_; ++i) {
            new (&data_[i - 1]) T(static_cast<T&&>(data_[i]));
            data_[i].~T();
        }

        --size_;
    }

    void insert(uint64_t index, const T& value) {
        assert(index <= size_);

        if (size_ >= capacity_) {
            grow();
        }

        if (index < size_) {
            new (&data_[size_]) T(static_cast<T&&>(data_[size_ - 1]));
            if (size_ > 1) {
                for (uint64_t i = size_ - 1; i > index; --i) {
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

    // Sort the vector using the provided comparison function
    // Uses a hybrid quicksort with insertion sort for small partitions
    template<typename Compare>
    void sort(Compare comp) {
        if (size_ <= 1) {
            return;
        }
        quicksort(0, size_ - 1, comp);
    }

private:
    // Threshold for switching to insertion sort
    static const uint64_t INSERTION_SORT_THRESHOLD = 16;

    // Swap two elements
    void swap(uint64_t i, uint64_t j) {
        assert(i < size_);
        assert(j < size_);
        if (i != j) {
            T temp(static_cast<T&&>(data_[i]));
            data_[i] = static_cast<T&&>(data_[j]);
            data_[j] = static_cast<T&&>(temp);
        }
    }

    // Insertion sort for small partitions
    template<typename Compare>
    void insertionSort(uint64_t left, uint64_t right, Compare comp) {
        for (uint64_t i = left + 1; i <= right; ++i) {
            T key(data_[i]);
            uint64_t j = i;
            while (j > left && comp(key, data_[j - 1])) {
                data_[j] = static_cast<T&&>(data_[j - 1]);
                --j;
            }
            data_[j] = static_cast<T&&>(key);
        }
    }

    // Median-of-three pivot selection
    template<typename Compare>
    uint64_t medianOfThree(uint64_t left, uint64_t right, Compare comp) {
        assert(right >= left + 2); // Need at least 3 elements
        uint64_t mid = left + (right - left) / 2;

        // Sort left, mid, right so that data_[left] <= data_[mid] <= data_[right]
        if (comp(data_[mid], data_[left])) {
            swap(left, mid);
        }
        if (comp(data_[right], data_[left])) {
            swap(left, right);
        }
        if (comp(data_[right], data_[mid])) {
            swap(mid, right);
        }

        // Place median at right-1
        swap(mid, right - 1);
        return right - 1;
    }

    // Partition the array around a pivot
    template<typename Compare>
    uint64_t partition(uint64_t left, uint64_t right, uint64_t pivotIndex, Compare comp) {
        T pivotValue(data_[pivotIndex]);
        swap(pivotIndex, right);

        uint64_t storeIndex = left;
        for (uint64_t i = left; i < right; ++i) {
            if (comp(data_[i], pivotValue)) {
                swap(i, storeIndex);
                ++storeIndex;
            }
        }

        swap(storeIndex, right);
        return storeIndex;
    }

    // Quicksort implementation
    template<typename Compare>
    void quicksort(uint64_t left, uint64_t right, Compare comp) {
        // Guard against underflow: if left >= right, we're done
        if (left >= right || left >= size_ || right >= size_) {
            return;
        }

        // Use insertion sort for small partitions
        if (right - left + 1 <= INSERTION_SORT_THRESHOLD) {
            insertionSort(left, right, comp);
            return;
        }

        // Use median-of-three for pivot selection (requires at least 3 elements)
        uint64_t pivotIndex;
        if (right - left >= 2) {
            pivotIndex = medianOfThree(left, right, comp);
        } else {
            pivotIndex = left;
        }
        pivotIndex = partition(left, right, pivotIndex, comp);

        // Recursively sort left and right partitions
        // Check pivotIndex > left to avoid underflow when computing pivotIndex - 1
        if (pivotIndex > left) {
            quicksort(left, pivotIndex - 1, comp);
        }
        if (pivotIndex < right) {
            quicksort(pivotIndex + 1, right, comp);
        }
    }
    void grow() {
        uint64_t newCapacity = capacity_ == 0 ? 8 : capacity_ * 2;
        reserve(newCapacity);
    }

    T* data_;
    uint64_t size_;
    uint64_t capacity_;
    MemoryAllocator* allocator_;
    const char* callerId_;
};
