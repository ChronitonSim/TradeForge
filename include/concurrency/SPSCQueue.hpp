#include <cstddef>
#include <stdexcept>
#include <vector>
#include <atomic>
#include <optional>
#include <utility>
#include <new>

namespace tradeforge::concurrency {

template<typename T>
class SPSCQueue {
    private:
        // Cache line size to prevent false sharing.
        // If the compiler does not define it, fall back to 64.
        #ifdef __cpp_lib_hardware_interference_size
            static constexpr std::size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
        #else
            static constexpr std::size_t CACHE_LINE_SIZE = 64;
        #endif

        // The capacity must be a power of 2 to allow bitwise modulo optimization
        std::size_t capacity_;
        std::size_t mask_;
        std::vector<T> buffer_;

        // PRODUCER'S DOMAIN
        // Ensure this variable starts on its own cache line.
        // The producer writes to head_, the consumer only reads it.
        alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> head_{0};
        
        // CONSUMER'S DOMAIN
        // Padded to the next cache line.
        // The consumer writes to tail_, the producer only reads it.
        alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> tail_{0};

    public:
        // Constructor: pre-allocate the buffer to avoid later
        // heap allocations on the hot path.
        explicit SPSCQueue(std::size_t capacity) {
            // Validate power of 2 using bitwise &
            // capapcity is a power of 2 iff (capacity & (capacity - 1) == 0)
            if (capacity < 2 || (capacity & (capacity - 1)) != 0)
                throw std::invalid_argument("Capacity must be a power of 2.");
            
            capacity_ = capacity;
            mask_ = capacity - 1;
            buffer_.resize(capacity);
        }

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
            T item = std::move(buffer_[current_tail]);

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