# SPSCQueue: A Lock-Free Single-Producer Single-Consumer Ring Buffer

## 1. Overview and Purpose

The `SPSCQueue` is a high-performance, lock-free circular queue designed for safe, concurrent data transfer between exactly one producing thread and exactly one consuming thread. 

Its primary purpose is to eliminate the latency and context-switching overhead associated with kernel-level synchronization primitives (like `std::mutex` or `std::condition_variable`). By pre-allocating contiguous memory and utilizing atomic memory barriers, the queue achieves strict $O(1)$ operations, making it suitable for ultra-low-latency environments such as high-frequency trading, real-time audio processing, and low-level embedded systems.

## 2. Disjoint Access and Lock-Free Strength

The fundamental performance strength of this algorithm lies in the principle of **Disjoint Access**. The shared state is heavily partitioned to prevent cache-line contention (False Sharing) and algorithmic bottlenecks.

* **The Producer's Domain:** The producing thread has exclusive write access to the `head_` atomic index and exclusive write access to the empty slots in the buffer.
* **The Consumer's Domain:** The consuming thread has exclusive write access to the `tail_` atomic index and exclusive read access to the populated slots in the buffer.

Because the producer and consumer almost never try to step on each other's toes, they operate completely independently. The only point of logical contention occurs at the boundaries: when the queue is completely full (producer must wait) or completely empty (consumer must wait). During normal operation, both threads run at native hardware speed, synchronized only by the propagation of cache lines.

## 3. Synchronization Semantics

In a standard lock-free architecture, a boolean flag is often used to signal that a payload is ready. In a ring buffer, there are **two distinct payloads** and **two implicit flags**.

1.  **Payload A (Data):** Guarded by `head_`. The producer releases it; the consumer acquires it.
2.  **Payload B (Empty Space):** Guarded by `tail_`. The consumer releases it; the producer acquires it.

### The `!=` Condition as a Synchronization Flag
The synchronization "flag" in this queue is not a static integer; it is a relative state defined by the inequality of the indices.
* For the **Consumer**, the synchronization flag signaling "Data is Ready" is the condition `current_tail != head_.load(acquire)`.
* For the **Producer**, the synchronization flag signaling "Space is Ready" is the condition `next_head != tail_.load(acquire)`.

When these inequalities hold true, a strict **Happens-Before** relationship is established. The thread acquiring the index is guaranteed to see the memory operations performed by the counterparty before it released that index.

## 4. Execution Mechanics

Let $N$ be the capacity of the queue (must be a power of 2), and let $M$ be the bitwise mask $(N - 1)$. 

### Step-by-Step: The Producer (`push`)
1.  **Read Local State:** Load `current_head` using `memory_order_relaxed` (safe because only the producer modifies it).
2.  **Calculate Next Head:** Compute `next_head = (current_head + 1) & M`.
3.  **Acquire Consumer State:** Load `tail_` using `memory_order_acquire`. 
    * *Check:* If `next_head == tail_`, the queue is FULL. Return `false`.
4.  **Write Payload:** Safely write the item to `buffer_[current_head]`. (The `acquire` in Step 3 guarantees the consumer has finished reading from this slot).
5.  **Release State:** Store `next_head` into `head_` using `memory_order_release`. This publishes the data to the consumer.

### Step-by-Step: The Consumer (`pop`)
1.  **Read Local State:** Load `current_tail` using `memory_order_relaxed`.
2.  **Acquire Producer State:** Load `head_` using `memory_order_acquire`.
    * *Check:* If `current_tail == head_`, the queue is EMPTY. Return `std::nullopt`.
3.  **Read Payload:** Safely read the item from `buffer_[current_tail]`. (The `acquire` in Step 2 guarantees the producer has finished writing to this slot).
4.  **Calculate Next Tail:** Compute `next_tail = (current_tail + 1) & M`.
5.  **Release State:** Store `next_tail` into `tail_` using `memory_order_release`. This publishes the newly freed space to the producer.

## 5. Memory Visibility and Conservative Failure

Because cache coherence protocols take time to propagate updates across CPU cores, a thread executing an `acquire` load may read a "stale" (outdated) value of the counterparty's index. 

In this architecture, **stale reads are a feature, not a bug.** Because we only ever move indices forward, a stale read always presents a more conservative view of the world.

* **Consumer Reading a Stale `head_`:** A stale `head_` makes the consumer think the queue has less data than it actually does. If the consumer fails to see the most recent push, it may spuriously evaluate the queue as empty and safely abort. This prevents it from ever reading unwritten memory.
* **Producer Reading a Stale `tail_`:** A stale `tail_` makes the producer think the queue has less free space than it actually does. If the producer fails to see the most recent pop, it may spuriously evaluate the queue as full and safely abort. This prevents it from ever overwriting unread memory.

### Summary of Guarantees
The algorithm is designed to fail conservatively. The sync only happens when we *do* see the updated value, which guarantees the payload (either the data itself, or the empty space) is ready to be used. If the synchronization fails due to memory latency, the thread simply defers its work to the next polling cycle, maintaining absolute memory safety without requiring locks.

## 6. The Necessity of Acquire Semantics (Control Dependencies vs. Memory Barriers)

Can the `memory_order_acquire` semantics can be relaxed to `memory_order_relaxed` when reading the counterparty's index? Since the load occurs inside an `if` statement (e.g., `if (next_head == tail_.load(relaxed))`), one might intuitively assume that the CPU's branch prediction and **Control Dependencies** inherently prevent the subsequent buffer write from executing before the load completes.

This assumption is fundamentally flawed at both the compiler level and the hardware level.

### The Compiler Barrier
The C++ abstract machine does not respect control dependencies for memory synchronization. `memory_order_relaxed` would instruct the compiler that these memory operations have no ordering constraints. Under the "as-if" rule, the compiler would be permitted to load `tail_` and simultaneously begin preparing the memory addresses for the subsequent buffer write, emitting assembly that completely lacks hardware synchronization barriers.

### The Hardware Failure (Weakly-Ordered Architectures)
Even if the compiler emits the instructions in the exact lexical order, weakly-ordered CPUs (such as ARM or PowerPC) will aggressively reorder memory operations at runtime. 

If a `relaxed` load is used, the CPU might execute the following sequence:
1. The CPU initiates the load of `tail_`.
2. The branch predictor guesses the queue is not full and speculatively evaluates the `if` branch as false.
3. The CPU executes the write to `buffer_[current_head]` and places it in the Store Buffer.
4. **The Hazard:** Because the load was `relaxed`, the memory controller is architecturally permitted to commit that write to the L1 cache *before* it has definitively resolved the most up-to-date value of `tail_` from coherent memory. 

The CPU evaluates the branch using a potentially stale cache value, executes the write, and commits it. This completely severs the Happens-Before relationship, leading to data races and silent memory corruption. `memory_order_acquire` forces the compiler to emit a strict hardware barrier (e.g., `DMB ISHLD` on ARM), halting subsequent memory commits until the load is globally resolved.

### The x86 Illusion (Total Store Order)
This error is frequently masked on x86 architectures due to its **Total Store Order (TSO)** memory model. Under x86 TSO, all hardware loads inherently carry `acquire` semantics, and all hardware stores inherently carry `release` semantics. 

Consequently, on an x86 machine, `tail_.load(memory_order_relaxed)` and `tail_.load(memory_order_acquire)` will often compile to the exact same raw `MOV` assembly instruction. The code will physically execute safely, creating a dangerous illusion of correctness. However, this safety relies on an artifact of the specific hardware rather than the guarantees of the C++ memory model, rendering the lock-free data structure non-portable.

## 7. Concrete Execution Scenarios

To crystallize these abstract mechanics, consider a queue with a capacity of 4 (mask = 3). 

### Scenario A: Successful Synchronization
<p align="center">
<img src="art/1.png" alt="SPSCQueue Diagram 1" width="75%">
</p>

* **State:** `head_ = 0`, `tail_ = 0` (Empty).
* **Producer Push:**
    1. Reads `current_head` (0), computes `next_head` (1).
    2. Executes `tail_.load(acquire)` and successfully reads `0`.
    3. Evaluates `1 != 0`. Writes item 'A' to `buffer_[0]`.
    4. Executes `head_.store(1, release)`.

<p align="center">
<img src="art/2.png" alt="SPSCQueue Diagram 2" width="75%">
</p>

* **Consumer Pop:**
    1. Reads `current_tail` (0).
    2. Executes `head_.load(acquire)` and successfully reads `1`.
    3. Evaluates `0 != 1`. Reads item 'A' from `buffer_[0]`.
    4. Executes `tail_.store(1, release)`.

<p align="center">
<img src="art/3.png" alt="SPSCQueue Diagram 3" width="75%">
</p>

### Scenario B: Producer Stale Read (Spurious Full)
* **State:** `head_ = 3`. The consumer has just read `buffer_[0]` and updated `tail_` to `1`.
* **Producer Push:**
    1. Reads `current_head` (3), computes `next_head` (0). (Wrap-around).
    2. Executes `tail_.load(acquire)`. The cache update hasn't propagated, so it reads the stale value `0`.
    3. Evaluates `0 == 0`. The producer assumes the queue is full and returns `false`.
* **Safety Guarantee:** The producer safely aborts. Even though `buffer_[0]` is technically free, the producer conservatively waits until the `tail_ = 1` update physically reaches its CPU core, strictly preventing the overwriting of unread memory.

### Scenario C: Consumer Stale Read (Partial Batch)
* **State:** `current_tail = 1`. The producer has rapidly written to `buffer_[1]` (releasing `head_ = 2`) and `buffer_[2]` (releasing `head_ = 3`). Actual `head_` in main memory is `3`.
* **Consumer Pop:**
    1. Reads `current_tail` (1).
    2. Executes `head_.load(acquire)`. It only sees the first update, reading the stale value `2`.
    3. Evaluates `1 != 2`. The consumer does not return empty.
* **Safety Guarantee:** Because the consumer acquired the value `2`, it is guaranteed to see all memory operations the producer performed *before* releasing `2`. It safely reads `buffer_[1]` and updates `tail_` to `2`. It ignores `buffer_[2]` for now. The consumer conservatively processes a "partial batch" and defers the rest, ensuring it never reads a buffer slot before the producer's write is fully visible.


### Scenario D: The Impossible Stale Local Read
It is impossible for the Producer to read a stale `head_` or for the Consumer to read a stale `tail_` during their initial `memory_order_relaxed` loads. This is because of Disjoint Access: the Producer is the *sole writer* to `head_` and the Consumer is the *sole writer* to `tail_`. 

* **Hardware Guarantee (Store-to-Load Forwarding):** A thread is guaranteed to see its own most recent writes sequentially. Even if a thread's previous write is still sitting in its local CPU Store Buffer and hasn't flushed to the L1 cache, the CPU will automatically forward that pending value to the subsequent read instruction, bypassing the L1 cache entirely.
* **Algorithm Implication:** The local `relaxed` loads will always return the most up-to-date local state. Cache propagation latency only applies when reading a variable modified by the *counterparty* thread (which is handled by the `acquire` barriers). Therefore, local reads never fail, and local state is never stale.

## 8. Micro-Optimizations

### Zero-Copy and NRVO in `pop()`
The `pop()` method returns a `std::optional<T>`. A naive implementation might move the buffer item into a local variable of type `T` before returning it:

```cpp
// Sub-optimal: Prevents NRVO
T item = std::move(buffer_[current_tail]);
// ...
return item; 
```

Because the local variable type (`T`) does not strictly match the function's return type (`std::optional<T>`), the C++ standard prohibits the compiler from performing Named Return Value Optimization (NRVO). This forces an implicit conversion upon return, resulting in two distinct move operations: one from the buffer to the local variable, and another from the local variable into the caller's `std::optional` return slot (followed by a destructor call for the local variable).

To achieve optimal efficiency, we perfectly align the types by constructing the `std::optional<T>` directly:

```cpp
// Optimal: Enables NRVO
std::optional<T> item{std::move(buffer_[current_tail])};
// ...
return item;
```

With matching types, the compiler applies NRVO. It maps the memory address of the local `item` directly to the return slot in the caller's stack frame. The payload is moved exactly once from the ring buffer directly to its final destination. Since the structural footprint of `std::optional` is merely a boolean flag and alignment padding, this approach incurs zero extra runtime overhead, achieving a true zero-copy pipeline out of the queue.

### Bitwise Arithmetic

In ultra-low-latency environments, standard arithmetic operations can introduce unacceptable hardware stalls. The CPU `DIV` (division) instruction, required for the modulo operator (`%`), typically consumes 15 to 20 clock cycles. The `SPSCQueue` bypasses this penalty by enforcing a power-of-two capacity, allowing the reduction of division arithmetic into single-cycle bitwise logic.

#### The Bitwise AND Operator (`&`)
The bitwise AND operator evaluates the binary representations of two integers. For each corresponding pair of bits $a$ and $b$, the operation $a \land b$ evaluates to $1$ if and only if both $a = 1$ and $b = 1$. Otherwise, it evaluates to $0$.

#### Proposition 1: Power of Two Validation
**Statement:** An integer $N \ge 2$ is a power of $2$ if and only if $N \land (N - 1) = 0$.

**Proof/Mechanism:** If $N = 2^k$, its binary representation consists of a single $1$ at the $k$-th position, followed entirely by $k$ zeros. Subtracting $1$ from $N$ inverts this structure: the $k$-th bit flips to $0$, and all $k$ trailing zeros flip to $1$. Because the bits of $N$ and $N-1$ are perfectly inverted, no two corresponding bits can simultaneously be $1$. Consequently, the bitwise AND operation yields $0$.

**Example 1: $N = 8$ (Power of 2)**
* $N = 8 \implies 1000_2$
* $N - 1 = 7 \implies 0111_2$
* $1000_2 \land 0111_2 = 0000_2 = 0$

**Example 2: $N = 10$ (Not a Power of 2)**
* $N = 10 \implies 1010_2$
* $N - 1 = 9 \implies 1001_2$
* $1010_2 \land 1001_2 = 1000_2 = 8 \neq 0$

**Application:** This proposition is evaluated in the queue's constructor (`(capacity & (capacity - 1)) != 0`) to guarantee the queue's size is a power of $2$.

#### Proposition 2: Fast Modulo Arithmetic
**Statement:** Let $N = 2^k$ for some positive integer $k$. For any non-negative integer $X$, the modulo operation $X \pmod N$ is equivalent to $X \land N-1$.

**Proof/Mechanism:**
To understand why this equivalence holds, we must examine how division operates in base-2, and how a bitwise mask physically isolates a mathematical remainder.

**1. The Nature of the Remainder (The Least Significant Bits)**
In any numeral system, dividing a number by its base raised to a power $k$ naturally isolates the $k$ rightmost digits as the remainder. For instance, in base-10, dividing $1234$ by $10^2$ yields a quotient of $12$ and a remainder of $34$. 

This exact law applies to binary (base-2). Dividing an integer $X$ by $N = 2^k$ is mechanically equivalent to performing a right bit-shift on $X$ by $k$ positions. The bits that are shifted past the decimal point—the $k$ least significant bits (the rightmost bits)—constitute the remainder. Therefore, computing $X \pmod{2^k}$ simply requires extracting the $k$ rightmost bits of $X$.

**2. The Anatomy of the Mask**
The mask $M = 2^k - 1$ is structurally designed to perform this very extraction. In binary, $M$ is composed of exactly $k$ consecutive $1$ s starting from the least significant bit, with all higher-order bits (to the left) implicitly set to $0$. 

**3. The Action of Bitwise AND**
When we apply the bitwise AND ($\land$) operation between $X$ and the mask $M$, the mask exerts a dual action on the binary sequence:
* **Annihilation:** The $0$ s on the left side of the mask multiply the higher-order bits of $X$ by zero, completely annihilating the quotient.
* **Preservation:** The $1$ s on the right side of the mask act as the identity element, since any bit ANDed with $1$ retains its original value. This preserves the $k$ least significant bits.

By annihilating the quotient and preserving the remainder, the mask computes the modulo in a single CPU clock cycle, bypassing the expensive hardware division instruction.

**Example 1: Isolating the Remainder**
Assume capacity $N = 4$ ($k=2$), so the mask $M = 3$. Let $X = 13$.
Mathematically, $13 \pmod 4 = 1$. Let us look at the bits:
* $X = 13 \implies 1101_2$
* $M = 3 \implies 0011_2$

```text
  1101  (X = 13)
& 0011  (Mask M = 3)
  ----
  0001  (Result is 1)
```
The mask's zeros annihilate the `11` on the left, while the mask's ones preserve the `01` on the right.

**Example 2: Index Increment (The Identity Function)**
Assume capacity $N = 8$, mask $M = 7$. The `current_head` is $4$. We increment by $1$ so $X = 5$.
* $X = 5 \implies 0101_2$
* $M = 7 \implies 0111_2$
* $0101_2 \land 0111_2 = 0101_2 = 5$
The mask preserves the value, naturally advancing the index.

**Example 3: Ring Buffer Wrap-Around**
Assume capacity $N = 8$ ($k=3$), so the mask $M = 7$. 
The `current_head` is $7$ (the very end of the buffer). We increment the index by $1$, so our new target is $X = 8$. We need the index to wrap back around to $0$.
* $X = 8 \implies 1000_2$
* $M = 7 \implies 0111_2$

```text
  1000  (X = 8)
& 0111  (Mask M = 7)
  ----
  0000  (Result is 0)
```
The mask annihilates the $4$-th bit. The out-of-bounds index $8$ is mapped to $0$, wrapping the head pointer back to the beginning of the ring buffer.

**Application:** This is utilized during the increment phase of both the producer and the consumer: `next_head = (current_head + 1) & mask_`.