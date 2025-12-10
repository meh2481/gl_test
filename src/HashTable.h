#pragma once

#include "MemoryAllocator.h"
#include <cstdint>
#include <cassert>

// Hash function for integral types
template<typename K>
inline uint32_t hashKey(const K& key) {
    // FNV-1a hash for general types
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&key);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(K); ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

// Specialization for pointer types
template<typename T>
inline uint32_t hashKey(T* const& ptr) {
    uintptr_t val = reinterpret_cast<uintptr_t>(ptr);
    uint32_t hash = static_cast<uint32_t>(val ^ (val >> 32));
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);
    return hash;
}

// Specialization for uint64_t (common in this codebase)
template<>
inline uint32_t hashKey<uint64_t>(const uint64_t& key) {
    uint32_t hash = static_cast<uint32_t>(key ^ (key >> 32));
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);
    return hash;
}

// Specialization for uint32_t
template<>
inline uint32_t hashKey<uint32_t>(const uint32_t& key) {
    uint32_t hash = key;
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);
    return hash;
}

// Specialization for int
template<>
inline uint32_t hashKey<int>(const int& key) {
    return hashKey(static_cast<uint32_t>(key));
}

// Template-based fast hash lookup table
// K = Key type (must be trivially copyable and support operator==)
// V = Value type (must be trivially copyable)
// Uses open addressing with linear probing for O(1) lookup
// Configurable memory allocator via MemoryAllocator interface
//
// Note: This hash table is designed for simple/POD types.
// It does not call constructors/destructors, so types with
// non-trivial constructors should not be used.
template<typename K, typename V>
class HashTable {
public:
    // Constructor with custom allocator
    explicit HashTable(MemoryAllocator& allocator)
        : keys_(nullptr)
        , values_(nullptr)
        , occupied_(nullptr)
        , capacity_(0)
        , size_(0)
        , allocator_(&allocator)
    {
        assert(allocator_ != nullptr);
        // Start with a reasonable default capacity
        reserve(16);
    }

    ~HashTable() {
        clear();
        if (keys_) {
            allocator_->free(keys_);
            keys_ = nullptr;
        }
        if (values_) {
            allocator_->free(values_);
            values_ = nullptr;
        }
        if (occupied_) {
            allocator_->free(occupied_);
            occupied_ = nullptr;
        }
    }

    // Disable copy constructor and assignment
    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;

    // Insert or update a key-value pair
    // Returns true if a new entry was inserted, false if an existing entry was updated
    bool insert(const K& key, const V& value) {
        assert(keys_ != nullptr);
        assert(values_ != nullptr);
        assert(occupied_ != nullptr);

        // Grow if load factor exceeds 0.7
        if (size_ * 10 >= capacity_ * 7) {
            reserve(capacity_ * 2);
        }

        uint32_t hash = hashKey(key);
        uint32_t index = hash % capacity_;
        uint32_t probeCount = 0;

        // Linear probing to find empty slot or existing key
        while (occupied_[index]) {
            if (keys_[index] == key) {
                // Update existing value
                values_[index] = value;
                return false;
            }
            index = (index + 1) % capacity_;
            probeCount++;
            assert(probeCount < capacity_); // Should never probe entire table
        }

        // Insert new entry
        keys_[index] = key;
        values_[index] = value;
        occupied_[index] = true;
        size_++;
        return true;
    }

    // Lookup a value by key
    // Returns pointer to value if found, nullptr otherwise
    V* find(const K& key) {
        if (size_ == 0) {
            return nullptr;
        }

        assert(keys_ != nullptr);
        assert(values_ != nullptr);
        assert(occupied_ != nullptr);

        uint32_t hash = hashKey(key);
        uint32_t index = hash % capacity_;
        uint32_t probeCount = 0;

        // Linear probing to find the key
        while (probeCount < capacity_) {
            if (!occupied_[index]) {
                // Empty slot means key not found
                return nullptr;
            }
            if (keys_[index] == key) {
                // Found the key
                return &values_[index];
            }
            index = (index + 1) % capacity_;
            probeCount++;
        }

        return nullptr;
    }

    // Const version of find
    const V* find(const K& key) const {
        if (size_ == 0) {
            return nullptr;
        }

        assert(keys_ != nullptr);
        assert(values_ != nullptr);
        assert(occupied_ != nullptr);

        uint32_t hash = hashKey(key);
        uint32_t index = hash % capacity_;
        uint32_t probeCount = 0;

        while (probeCount < capacity_) {
            if (!occupied_[index]) {
                return nullptr;
            }
            if (keys_[index] == key) {
                return &values_[index];
            }
            index = (index + 1) % capacity_;
            probeCount++;
        }

        return nullptr;
    }

    // Check if key exists
    bool contains(const K& key) const {
        return find(key) != nullptr;
    }

    // Remove a key-value pair
    // Returns true if the key was found and removed, false otherwise
    bool remove(const K& key) {
        if (size_ == 0) {
            return false;
        }

        assert(keys_ != nullptr);
        assert(values_ != nullptr);
        assert(occupied_ != nullptr);

        uint32_t hash = hashKey(key);
        uint32_t index = hash % capacity_;
        uint32_t probeCount = 0;

        // Find the key
        while (probeCount < capacity_) {
            if (!occupied_[index]) {
                return false;
            }
            if (keys_[index] == key) {
                // Mark as unoccupied
                occupied_[index] = false;
                size_--;

                // Rehash entries after this one to maintain probe chain
                uint32_t nextIndex = (index + 1) % capacity_;
                while (occupied_[nextIndex]) {
                    K rehashKey = keys_[nextIndex];
                    V rehashValue = values_[nextIndex];
                    occupied_[nextIndex] = false;
                    size_--;

                    // Reinsert
                    insert(rehashKey, rehashValue);

                    nextIndex = (nextIndex + 1) % capacity_;
                }

                return true;
            }
            index = (index + 1) % capacity_;
            probeCount++;
        }

        return false;
    }

    // Clear all entries
    void clear() {
        if (occupied_) {
            memset(occupied_, 0, capacity_ * sizeof(bool));
        }
        size_ = 0;
    }

    // Reserve capacity for at least n elements
    void reserve(uint32_t n) {
        assert(n > 0);
        if (n <= capacity_) {
            return;
        }

        // Allocate new arrays
        K* newKeys = static_cast<K*>(allocator_->allocate(n * sizeof(K)));
        V* newValues = static_cast<V*>(allocator_->allocate(n * sizeof(V)));
        bool* newOccupied = static_cast<bool*>(allocator_->allocate(n * sizeof(bool)));

        assert(newKeys != nullptr);
        assert(newValues != nullptr);
        assert(newOccupied != nullptr);

        memset(newOccupied, 0, n * sizeof(bool));

        // Rehash existing entries into new arrays
        if (keys_ && values_ && occupied_) {
            for (uint32_t i = 0; i < capacity_; ++i) {
                if (occupied_[i]) {
                    // Find slot in new table
                    uint32_t hash = hashKey(keys_[i]);
                    uint32_t newIndex = hash % n;

                    // Linear probing
                    while (newOccupied[newIndex]) {
                        newIndex = (newIndex + 1) % n;
                    }

                    newKeys[newIndex] = keys_[i];
                    newValues[newIndex] = values_[i];
                    newOccupied[newIndex] = true;
                }
            }

            // Free old arrays
            allocator_->free(keys_);
            allocator_->free(values_);
            allocator_->free(occupied_);
        }

        keys_ = newKeys;
        values_ = newValues;
        occupied_ = newOccupied;
        capacity_ = n;
    }

    // Get current number of entries
    uint32_t size() const {
        return size_;
    }

    // Get current capacity
    uint32_t capacity() const {
        return capacity_;
    }

    // Check if table is empty
    bool empty() const {
        return size_ == 0;
    }

    // Iterator for traversing all entries
    class Iterator {
    public:
        Iterator(HashTable* table, uint32_t index)
            : table_(table), index_(index)
        {
            // Move to first occupied slot
            while (index_ < table_->capacity_ && !table_->occupied_[index_]) {
                index_++;
            }
        }

        bool operator!=(const Iterator& other) const {
            return table_ != other.table_ || index_ != other.index_;
        }

        Iterator& operator++() {
            assert(index_ < table_->capacity_);
            index_++;
            // Move to next occupied slot
            while (index_ < table_->capacity_ && !table_->occupied_[index_]) {
                index_++;
            }
            return *this;
        }

        const K& key() const {
            assert(index_ < table_->capacity_);
            assert(table_->occupied_[index_]);
            return table_->keys_[index_];
        }

        V& value() {
            assert(index_ < table_->capacity_);
            assert(table_->occupied_[index_]);
            return table_->values_[index_];
        }

    private:
        HashTable* table_;
        uint32_t index_;
    };

    // Const iterator for traversing all entries
    class ConstIterator {
    public:
        ConstIterator(const HashTable* table, uint32_t index)
            : table_(table), index_(index)
        {
            // Move to first occupied slot
            while (index_ < table_->capacity_ && !table_->occupied_[index_]) {
                index_++;
            }
        }

        bool operator!=(const ConstIterator& other) const {
            return table_ != other.table_ || index_ != other.index_;
        }

        ConstIterator& operator++() {
            assert(index_ < table_->capacity_);
            index_++;
            // Move to next occupied slot
            while (index_ < table_->capacity_ && !table_->occupied_[index_]) {
                index_++;
            }
            return *this;
        }

        const K& key() const {
            assert(index_ < table_->capacity_);
            assert(table_->occupied_[index_]);
            return table_->keys_[index_];
        }

        const V& value() const {
            assert(index_ < table_->capacity_);
            assert(table_->occupied_[index_]);
            return table_->values_[index_];
        }

    private:
        const HashTable* table_;
        uint32_t index_;
    };

    Iterator begin() {
        return Iterator(this, 0);
    }

    Iterator end() {
        return Iterator(this, capacity_);
    }

    ConstIterator begin() const {
        return ConstIterator(this, 0);
    }

    ConstIterator end() const {
        return ConstIterator(this, capacity_);
    }

    ConstIterator cbegin() const {
        return ConstIterator(this, 0);
    }

    ConstIterator cend() const {
        return ConstIterator(this, capacity_);
    }

private:
    K* keys_;
    V* values_;
    bool* occupied_;
    uint32_t capacity_;
    uint32_t size_;

    MemoryAllocator* allocator_;
};
