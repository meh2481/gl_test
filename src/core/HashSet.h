#pragma once

#include <SDL3/SDL_stdinc.h>
#include "../memory/MemoryAllocator.h"
#include <cassert>

// Hash function for integral types (reusing from HashTable.h)
template<typename K>
inline Uint32 hashSetKey(const K& key) {
    // FNV-1a hash for general types
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&key);
    Uint32 hash = 2166136261u;
    for (Uint64 i = 0; i < sizeof(K); ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

// Specialization for pointer types
template<typename T>
inline Uint32 hashSetKey(T* const& ptr) {
    uintptr_t val = reinterpret_cast<uintptr_t>(ptr);
    Uint32 hash = static_cast<Uint32>(val ^ (val >> 32));
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);
    return hash;
}

// Specialization for Uint64 (common in this codebase)
template<>
inline Uint32 hashSetKey<Uint64>(const Uint64& key) {
    Uint32 hash = static_cast<Uint32>(key ^ (key >> 32));
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);
    return hash;
}

// Specialization for Uint32
template<>
inline Uint32 hashSetKey<Uint32>(const Uint32& key) {
    Uint32 hash = key;
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);
    return hash;
}

// Specialization for int
template<>
inline Uint32 hashSetKey<int>(const int& key) {
    return hashSetKey(static_cast<Uint32>(key));
}

// Template-based fast hash set
// K = Key type (must be trivially copyable and support operator==)
// Uses open addressing with linear probing for O(1) lookup
// Configurable memory allocator via MemoryAllocator interface
//
// Note: This hash set is designed for simple/POD types.
// It does not call constructors/destructors, so types with
// non-trivial constructors should not be used.
template<typename K>
class HashSet {
public:
    // Constructor with custom allocator and allocation ID
    explicit HashSet(MemoryAllocator& allocator, const char* callerId)
        : keys_(nullptr)
        , occupied_(nullptr)
        , capacity_(0)
        , size_(0)
        , allocator_(&allocator)
        , callerId_(callerId)
    {
        assert(allocator_ != nullptr);
        assert(callerId_ != nullptr);
        // Start with a reasonable default capacity
        reserve(16);
    }

    ~HashSet() {
        clear();
        if (keys_) {
            allocator_->free(keys_);
            keys_ = nullptr;
        }
        if (occupied_) {
            allocator_->free(occupied_);
            occupied_ = nullptr;
        }
    }

    // Disable copy constructor and assignment
    HashSet(const HashSet&) = delete;
    HashSet& operator=(const HashSet&) = delete;

    // Insert a key
    // Returns true if a new entry was inserted, false if the key already existed
    bool insert(const K& key) {
        assert(keys_ != nullptr);
        assert(occupied_ != nullptr);

        // Grow if load factor exceeds 0.7
        if (size_ * 10 >= capacity_ * 7) {
            reserve(capacity_ * 2);
        }

        Uint32 hash = hashSetKey(key);
        Uint32 index = hash % capacity_;
        Uint32 probeCount = 0;

        // Linear probing to find empty slot or existing key
        while (occupied_[index]) {
            if (keys_[index] == key) {
                // Key already exists
                return false;
            }
            index = (index + 1) % capacity_;
            probeCount++;
            assert(probeCount < capacity_); // Should never probe entire table
        }

        // Insert new entry
        keys_[index] = key;
        occupied_[index] = true;
        size_++;
        return true;
    }

    // Check if key exists
    bool contains(const K& key) const {
        return find(key) != nullptr;
    }

    // Find a key
    // Returns pointer to key if found, nullptr otherwise
    const K* find(const K& key) const {
        if (size_ == 0) {
            return nullptr;
        }

        assert(keys_ != nullptr);
        assert(occupied_ != nullptr);

        Uint32 hash = hashSetKey(key);
        Uint32 index = hash % capacity_;
        Uint32 probeCount = 0;

        // Linear probing to find the key
        while (probeCount < capacity_) {
            if (!occupied_[index]) {
                // Empty slot means key not found
                return nullptr;
            }
            if (keys_[index] == key) {
                // Found the key
                return &keys_[index];
            }
            index = (index + 1) % capacity_;
            probeCount++;
        }

        return nullptr;
    }

    // Remove a key
    // Returns true if the key was found and removed, false otherwise
    bool erase(const K& key) {
        if (size_ == 0) {
            return false;
        }

        assert(keys_ != nullptr);
        assert(occupied_ != nullptr);

        Uint32 hash = hashSetKey(key);
        Uint32 index = hash % capacity_;
        Uint32 probeCount = 0;

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
                Uint32 nextIndex = (index + 1) % capacity_;
                while (occupied_[nextIndex]) {
                    K rehashKey = keys_[nextIndex];
                    occupied_[nextIndex] = false;
                    size_--;

                    // Reinsert
                    insert(rehashKey);

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
            SDL_memset(occupied_, 0, capacity_ * sizeof(bool));
        }
        size_ = 0;
    }

    // Reserve capacity for at least n elements
    void reserve(Uint32 n) {
        assert(n > 0);
        if (n <= capacity_) {
            return;
        }

        // Allocate new arrays
        K* newKeys = static_cast<K*>(allocator_->allocate(n * sizeof(K), callerId_));
        bool* newOccupied = static_cast<bool*>(allocator_->allocate(n * sizeof(bool), callerId_));

        assert(newKeys != nullptr);
        assert(newOccupied != nullptr);

        SDL_memset(newOccupied, 0, n * sizeof(bool));

        // Rehash existing entries into new arrays
        if (keys_ && occupied_) {
            for (Uint32 i = 0; i < capacity_; ++i) {
                if (occupied_[i]) {
                    // Find slot in new table
                    Uint32 hash = hashSetKey(keys_[i]);
                    Uint32 newIndex = hash % n;

                    // Linear probing
                    while (newOccupied[newIndex]) {
                        newIndex = (newIndex + 1) % n;
                    }

                    newKeys[newIndex] = keys_[i];
                    newOccupied[newIndex] = true;
                }
            }

            // Free old arrays
            allocator_->free(keys_);
            allocator_->free(occupied_);
        }

        keys_ = newKeys;
        occupied_ = newOccupied;
        capacity_ = n;
    }

    // Get current number of entries
    Uint32 size() const {
        return size_;
    }

    // Get current capacity
    Uint32 capacity() const {
        return capacity_;
    }

    // Check if set is empty
    bool empty() const {
        return size_ == 0;
    }

    // Iterator for traversing all entries
    class Iterator {
    public:
        Iterator(HashSet* set, Uint32 index)
            : set_(set), index_(index)
        {
            // Move to first occupied slot
            while (index_ < set_->capacity_ && !set_->occupied_[index_]) {
                index_++;
            }
        }

        bool operator!=(const Iterator& other) const {
            return set_ != other.set_ || index_ != other.index_;
        }

        Iterator& operator++() {
            assert(index_ < set_->capacity_);
            index_++;
            // Move to next occupied slot
            while (index_ < set_->capacity_ && !set_->occupied_[index_]) {
                index_++;
            }
            return *this;
        }

        const K& operator*() const {
            assert(index_ < set_->capacity_);
            assert(set_->occupied_[index_]);
            return set_->keys_[index_];
        }

    private:
        HashSet* set_;
        Uint32 index_;
    };

    // Const iterator for traversing all entries
    class ConstIterator {
    public:
        ConstIterator(const HashSet* set, Uint32 index)
            : set_(set), index_(index)
        {
            // Move to first occupied slot
            while (index_ < set_->capacity_ && !set_->occupied_[index_]) {
                index_++;
            }
        }

        bool operator!=(const ConstIterator& other) const {
            return set_ != other.set_ || index_ != other.index_;
        }

        ConstIterator& operator++() {
            assert(index_ < set_->capacity_);
            index_++;
            // Move to next occupied slot
            while (index_ < set_->capacity_ && !set_->occupied_[index_]) {
                index_++;
            }
            return *this;
        }

        const K& operator*() const {
            assert(index_ < set_->capacity_);
            assert(set_->occupied_[index_]);
            return set_->keys_[index_];
        }

    private:
        const HashSet* set_;
        Uint32 index_;
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
    bool* occupied_;
    Uint32 capacity_;
    Uint32 size_;

    MemoryAllocator* allocator_;
    const char* callerId_;
};
