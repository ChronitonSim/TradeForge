#include <cstddef>
#include <iostream>
#include <optional>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <optional>
#include <iomanip>

#include "Timer.hpp"
#include "concurrency/SPSCQueue.hpp"

// --- BASELINE: STANDARD LOCK-BASED QUEUE ---
template<typename T>
class LockBasedQueue {
    private:
        std::queue<T> queue_;
        std::mutex mutex_;
        std::condition_variable cv_;

    public:
        void push(const T& item) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                queue_.push(item);
            }
            cv_.notify_one();
        }

        T pop() {

            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]{ return !queue_.empty(); });

            T item = queue_.front();
            queue_.pop();

            return item;
        }
};

// --- BENCHMARK PARAMETERS ---
constexpr std::size_t NUM_MESSAGES = 100'000'000;
constexpr std::size_t QUEUE_CAPACITY = 65'536; // 2^16

// --- TESTS ---
void run_lock_based_benchmark() {

    LockBasedQueue<std::size_t> queue;
    std::size_t consumer_sum = 0;

    Timer timer;

    std::thread producer ([&] {
        for (std::size_t i = 1; i <= NUM_MESSAGES; ++i)
            queue.push(i);
    });    

    std::thread consumer ([&] {
        for (std::size_t i = 1; i <= NUM_MESSAGES; ++i)
            consumer_sum += queue.pop();
    });

    producer.join();
    consumer.join();

    double elapsed = timer.elapsed_milliseconds();
    std::cout << "Lock-based queue time = " << std::fixed 
              << std::setprecision(2) << elapsed << " ms\n";
    
    if (consumer_sum == 0)
        std::cout << "Error: the compiler optimized away the loop.\n";
}

void run_lock_free_benchmark() {

    tradeforge::concurrency::SPSCQueue<std::size_t, QUEUE_CAPACITY> queue;
    std::size_t consumer_sum = 0;

    Timer timer;

    std::thread producer([&] {
        for (std::size_t i = 1; i <= NUM_MESSAGES; ++i) {
            // If the queue is full, spin on the CPU until a slot is available.
            // Do not sleep to avoid a context-switch penalty.
            while(!queue.push(i)) {
                // We could use a CPU pause instruction here,
                // but for raw throughput testing, an empty spin is fine.
            }
        }
    });

    std::thread consumer([&] {
        for (std::size_t i = 1; i <= NUM_MESSAGES; ++i) {
            std::optional<std::size_t> item;
            // Spin while the queue is empty.
            while(!(item = queue.pop()));
            consumer_sum += *item;
        }
    });

    producer.join();
    consumer.join();

    double elapsed = timer.elapsed_milliseconds();
    std::cout << "Lock-free queue time = " << std::fixed 
              << std::setprecision(2) << elapsed << " ms\n";
    
    if (consumer_sum == 0)
        std::cout << "Error: the compiler optimized away the loop.\n";
}

int main() {
    std::cout << "Starting SPSC Queue Benchmark with " << NUM_MESSAGES << " messages.\n";
    std::cout << "--------------------------------------------------------\n";
    
    run_lock_based_benchmark();
    run_lock_free_benchmark();

    std::cout << "--------------------------------------------------------\n";
    return 0;
}
