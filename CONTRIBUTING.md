# Contributing to TradeForge

Welcome to TradeForge! To maintain a clean, readable, and automated Git history, this project strictly adheres to the **Conventional Commits** specification. 

## Commit Message Format

Every commit message should be structured as follows:

`type(scope): subject`

### 1. Types
* **`feat`**: A new feature or architectural component.
* **`fix`**: A bug fix.
* **`perf`**: A code change that specifically improves performance or reduces latency.
* **`test`**: Adding missing tests or benchmarks.
* **`refactor`**: A code change that neither fixes a bug nor adds a feature (e.g., restructuring code).
* **`chore`**: Tooling, CMake updates, dependencies, or build process changes.
* **`docs`**: Arcitectural documentation (e.g. performance, control flow).

### 2. Scopes
To quickly identify which part of the trading engine is being modified, use one of the following scopes:
* **`spsc`**: Single-Producer Single-Consumer queue components.
* **`mpmc`**: Multi-Producer Multi-Consumer queue components.
* **`lob`**: Limit Order Book and object pool memory managers.
* **`net`**: The ingress, networking, and market data parsing thread.
* **`bench`**: Google Benchmark configurations and latency measurement tools.

### Examples
* `feat(spsc): implement memory-aligned atomic indices`
* `perf(lob): replace std::map with zero-allocation object pool`
* `test(bench): add 99th percentile latency tracking for hot path`
* `chore: update CMakeLists to require C++20` *(Note: Scope is optional for repository-wide chores)*
