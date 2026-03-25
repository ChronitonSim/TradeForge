#include <cstddef>
#include <array>
#include <atomic>
#include <optional>
#include <utility>
#include <new>

namespace tradeforge::concurrency {

template<typename T, std::size_t Capacity>
class SPSCQueue {
    
    private:

        // --- COMPILE-TIME VALIDATION ---
        // Avoids validation overhead at construction time 
        // and possible exceptions.
        static_assert(Capacity >= 2, "Capacity must be at least 2." );
        // The capacity must be a power of 2 to allow bitwise modulo optimizations
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2." );

        // Cache line size to prevent false sharing.
        // If the compiler does not define it, fall back to 64.
        #ifdef __cpp_lib_hardware_interference_size
            static constexpr std::size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
        #else
            static constexpr std::size_t CACHE_LINE_SIZE = 64;
        #endif

        // As a compile-time constant (rather than a member variable)
        // mask_ takes up no memory in the object footprint.
        static constexpr std::size_t mask_ = Capacity - 1;

        // Allocate buffer_ on the stack using std::array. 
        // No heap allocations.
        std::array<T, Capacity> buffer_;

        // PRODUCER'S DOMAIN
        // Ensure this variable starts on its own cache line.
        // The producer writes to head_, the consumer only reads it.
        alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> head_{0};
        
        // CONSUMER'S DOMAIN
        // Padded to the next cache line.
        // The consumer writes to tail_, the producer only reads it.
        alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> tail_{0};

    public:

        explicit SPSCQueue() = default;
            
        // --- PRODUCER METHODS ---

        // Copy Push (for lvalues)
        // Pushes an item onto the queue. Returns false if the queue is full.
        bool push(const T& item) {
            // Read the current head. No inter-thread sync required
            // because only the producer modifies head_. Thus,
            // relaxed memory ordering suffices.
            const std::size_t current_head = head_.load(std::memory_order_relaxed);

            // Calculate where the head will go next, using a fast bitwise modulo.
            // mask is capacity_'s power of two minus one. It acts as the neutral
            // element of bitwise & for all positive integers <= itself, and
            // wraps around to zero when acting on itself plus one (i.e. capacity_).
            const std::size_t next_head = (current_head + 1) & mask_;

            if (next_head == tail_.load(std::memory_order_acquire))
                // The head has caught up to the tail, and there are no more slots
                // available for the next head. The queue is full.
                return false; 

            // The queue is not full. We write data to the head of the buffer.
            // No need for inter-thread sync, as only the producer writes 
            // to this end of the queue.
            buffer_[current_head] = item;  // Copy happens here.

            // Publish the new head. 
            // Release semantics guarantee that the data written to 
            // buffer_[current_head] is visible to coherent memory before 
            // the head_ variable is updated.
            head_.store(next_head, std::memory_order_release);

            return true;
        }

        // Move Push (for rvalues)
        bool push(T&& item) {
            
            const std::size_t current_head = head_.load(std::memory_order_relaxed);
            const std::size_t next_head = (current_head + 1) & mask_;

            if (next_head == tail_.load(std::memory_order_acquire))
                return false; 

            buffer_[current_head] = std::move(item);
            head_.store(next_head, std::memory_order_release);

            return true;
        }

        // --- CONSUMER METHOD ---

        // Pops an item from the queue. Returns std::nullopt if the queue is empty.
        std::optional<T> pop() {
            // Read the current tail. Only the consumer modifies tail_, 
            // so relaxed is enough.
            const std::size_t current_tail = tail_.load(std::memory_order_relaxed);

            // Read the producer's head. Acquire semantics ensure that, after seeing
            // the new head, we are guaranteed to see the data the producer wrote 
            // before updating it. 
            if (current_tail == head_.load(std::memory_order_acquire))
                // The queue is empty.
                return std::nullopt;

            // Move the data from the tail of the queue.
            // Perfectly matching the type of the returned item to the return type
            // of the method enables Named Return Value Optimization. The compiler
            // will move the item directly into the caller's stack frame.
            std::optional<T> item{std::move(buffer_[current_tail])};

            // Calculate the next tail position.
            const std::size_t next_tail = (current_tail + 1) & mask_;

            // Publish the new tail. Release semantics ensure that we are done
            // reading (moving from) buffer_[current_tail], and writing to next_tail
            // before the tail_ variable is updated.
            tail_.store(next_tail, std::memory_order_release);

            return item;
        }

};


} // namespace tradeforge::concurrency