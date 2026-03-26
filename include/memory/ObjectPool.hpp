#pragma once

#include <cstddef>
#include <array>

namespace tradeforge::memory {

template<typename T, std::size_t Capacity>
class ObjectPool {

    private:
        // The pre-allocated memory arena to hold 
        // the actual data objects.
        std::array<T, Capacity> arena_;

        // The free list: a stack of available indices.
        std::array<std::size_t, Capacity> free_indices_;

        // The number of items currently available 
        // in the free list.
        std::size_t free_count_;

    public:
        // Constructor runs on the cold path.
        ObjectPool(): free_count_(Capacity) {
            // Initialize the free list
            // with all available indices
            for (std::size_t i = 0; i < Capacity; ++i)
                free_indices_[i] = i;
        }

        // --- HOT PATH METHODS ---

        // Acquire a free object from the pool.
        // Return nullptr if the pool is exhausted.
        T* acquire() noexcept {
            if (free_count_ == 0) return nullptr;

            // Decrease free_count_ to get the next available
            // free index (index to a free object in the pool).
            std::size_t index = free_indices_[--free_count_];

            // Return a pointer to the free object.
            return &arena_[index];
        }

        // Returns an object back to the pool.
        void release(T* ptr) noexcept {
            if (ptr == nullptr) return;

            // Pointer arithmetic to deduce the original index
            // of the object without performing a linear search
            // on the arena. We subtract the base address of the 
            // arena from the object's address.
            std::size_t index = static_cast<std::size_t>(ptr - arena_.data());

            // Push the index back onto the free list stack.
            free_indices_[free_count_++] = index;
        }

        // Utility to check remaining capacity.
        std::size_t available() const noexcept {
            return free_count_;
        }
};



} // namespace tradeforge::memory