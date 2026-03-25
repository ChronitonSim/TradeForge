# TradeForge
TradeForge is a C++20 High-Frequency Trading (HFT) architecture study focused on ultra-low latency. It implements a zero-allocation hot path using lock-free SPSC/MPMC queues, memory-aligned atomics, and custom object pools, bypassing OS context switches to achieve deterministic, microsecond-scale market data processing.

## Development Standards

This project follows the Conventional Commits specification for version control history. 

If you are contributing or reviewing the code, please refer to our [CONTRIBUTING.md](CONTRIBUTING.md) for the required commit message formats and architectural scopes.

---

## Phase 1: The Lock-Free SPSC Queue

### Architecture & Objective
The first bottleneck in a trading engine is passing high-volume market data (ticks) from the network ingress thread to the strategy processing thread. 

Using a standard `std::queue` protected by a `std::mutex` and `std::condition_variable` introduces severe latency. Under high contention, a mutex forces the thread to make a system call, yielding control to the operating system kernel and putting the thread to sleep. 

Phase 1 replaces this with `SPSCQueue`, a Single-Producer Single-Consumer lock-free ring buffer. It uses `std::atomic` variables and memory barriers (`acquire`/`release`) to pass data. The predicted benefit is that threads will remain entirely in userspace, syncing data via the CPU's L1 cache coherence protocol rather than OS interrupts. 

*(For a deep dive into the memory models and bitwise optimizations used, see the dedicated [SPSCQueue README](include/concurrency/README.md).)*

### Benchmark Results (100,000,000 messages)
*Test parameters: 1 Producer thread, 1 Consumer thread. Averaged over 5 runs.*

| Implementation | Threads | Execution Time (ms) |
| :--- | :--- | :--- |
| Lock-Based Queue (`std::mutex`) | 2 | 9318 ± 1795 |
| Lock-Free SPSC Queue | 2 | 561 ± 43 |

### Architectural Analysis: Performance and Determinism

The benchmark demonstrates a ~16.6x reduction in execution time, but more importantly, it exposes the cost of relying on the operating system for real-time performance.

**1. The Cost of Kernel Synchronization (Performance)**
The 9.3-second execution time of the lock-based queue illustrates the overhead of context switching. When the queue is empty or highly contended, the consumer thread enters the kernel to sleep, and the producer thread must later enter the kernel to wake it up. The lock-free queue processes the same 100 million messages in roughly 560 milliseconds because the threads never yield the CPU, avoiding system calls entirely.

**2. The Cost of the Scheduler (Determinism)**
The data shows a massive discrepancy in execution variance: the lock-based queue fluctuates by nearly 20% (± 1795 ms), while the lock-free queue maintains a tight bound of ~7% (± 43 ms). 

This aligns with established systems theory regarding general-purpose operating systems like Linux. When a thread goes to sleep on a `condition_variable`, its wake-up time is at the mercy of the OS scheduler, which may delay the thread to process background tasks or network interrupts. The lock-free benchmark forces the threads to "spin" (busy-wait) in a `while` loop when the queue is empty. While this consumes 100% of the CPU core, it prevents the OS scheduler from descheduling the thread, trading power efficiency for a deterministic, low-variance latency profile.