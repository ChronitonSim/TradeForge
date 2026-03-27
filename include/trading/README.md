# Trading Types and Data Structures

## 1. Overview
The `Types.hpp` file defines the fundamental data structures used throughout the TradeForge execution engine, most notably the network `Tick` and the resting `Order`. In ultra-low-latency systems, data structures are not just logical containers; they are physical memory blueprints optimized for CPU cache lines and network serialization.

## 2. Memory Layout: Strict-Width Integers
Standard C++ integer types (such as `int` or `long`) only provide minimum size guarantees, meaning their actual physical footprint varies depending on the operating system and the compiler.

To guarantee cross-platform Application Binary Interface (ABI) stability, TradeForge relies entirely on strict-width integers from the `<cstdint>` library (e.g., `std::uint32_t`, `std::uint64_t`). This enforces two critical hardware requirements:

1.  **Network Serialization Integrity:** When the ingress thread reads a binary market data message from an exchange over UDP, the application must map that byte stream to a `Tick` struct. If compiler variances alter the size of the struct, the application will read misaligned memory, resulting in corrupted market data.
2.  **Cache Line Packing:** High-performance data structures are padded to fit within 64-byte L1 cache lines. Using strict-width integers and single-byte enums (`enum class Side : std::uint8_t`) allows the compiler to generate a predictable memory footprint, preventing false sharing and maximizing memory bandwidth.

## 3. Limit Order Book Topology (Price-Time Priority)
A Limit Order Book (LOB) is a financial matching engine that organizes resting orders using **Price-Time Priority**. 
* **Price:** Orders are grouped by their designated price level.
* **Time:** Within a specific price level, orders form a First-In-First-Out (FIFO) queue. The first order placed at a given price is the first one executed.

### The Cancellation Bottleneck
Managing a single price level requires supporting three operations: Adding to the back (New Order), removing from the front (Execution), and removing from the middle (Cancellation). 

In modern equity markets, a vast majority of orders are canceled before they are executed. If a price level were implemented using a contiguous array (like `std::vector`), canceling an order in the middle of the queue would incur an $O(N)$ penalty, as the CPU must physically shift every subsequent order one slot to the left to close the memory gap. In a high-throughput environment, shifting arrays destroys execution latency. 

To achieve $O(1)$ cancellations, the price level must be implemented as a **doubly linked list**. When an order is canceled, the engine bypasses the $O(N)$ shift by simply updating the pointers of the adjacent orders to bridge the gap.

## 4. The Intrusive Linked List Architecture
While a doubly linked list is topologically required, the C++ Standard Library's implementation (`std::list`) introduces unacceptable overhead for the hot path.

### The `std::list` Allocation Penalty
When inserting an object into a `std::list`, the standard library dynamically allocates a hidden internal node on the heap. This node contains the object itself, plus the `prev` and `next` routing pointers. This design triggers the OS memory allocator for every incoming order, causing context switches, global lock contention, and cache-destroying memory fragmentation.

### The Intrusive Solution
TradeForge eliminates this overhead by using an **Intrusive Linked List**. Instead of wrapping the data inside a standard library node, the routing pointers (`Order* prev` and `Order* next`) are baked directly into the `Order` struct itself.

Because the `Order` object is its own list node, the Engine Thread can acquire a pre-allocated `Order` from the zero-allocation `ObjectPool`, populate its financial data, and wire its pointers directly into the LOB hierarchy. This architecture maintains the $O(1)$ cancellation speed of a linked list while bypassing the operating system's memory manager entirely.