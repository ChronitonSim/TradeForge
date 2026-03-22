# TradeForge
radeForge is a C++20 High-Frequency Trading (HFT) architecture study focused on ultra-low latency. It implements a zero-allocation hot path using lock-free SPSC/MPMC queues, memory-aligned atomics, and custom object pools, bypassing OS context switches to achieve deterministic, microsecond-scale market data processing.
