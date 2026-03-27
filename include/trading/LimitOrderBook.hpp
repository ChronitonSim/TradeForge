#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "Types.hpp"
#include "memory/ObjectPool.hpp"

namespace tradeforge::trading {

// --- THE INTRUSIVE LIST MANAGER ---
// This struct represents a single price level 
// (e.g. all buyers at $150.00) in the form of 
// a doubly-linked queue. It manages the Order 
// pointers, but does not allocate any memory itself.
struct PriceLevel {

    Order* head = nullptr; // First person in line (highest time priority).
    Order* tail = nullptr; // Last person in line.

    // O(1) Push to the back of the line.
    // order->next points in the direction of lower priority.
    void push_back(Order* order) noexcept {
        if (!head) {
            // The line is empty.
            head = tail = order;
        } else {
            // Put the order at the back and link it.
            tail->next = order;
            order->prev = tail;
            tail = order;
        }
    }

    // O(1) removal from anywhere in the line.
    void remove(Order* order) noexcept {
        // Wire the previous order to the next order.
        if (order->prev) {
            order->prev->next = order->next;
        } else {
            // If there is no prev, order is the head.
            // No need to check if (!order->next), because if order is
            // the only element in the queue, then 
            // head = tail = order, and
            // order->next = order->prev = nullptr
            // so the next line simply sets head = nullptr.
            head = order->next;
        }

        // Wire the next order to the previous order.
        if (order->next) {
            order->next->prev = order->prev;
        } else {
            // If there is no next, order is the tail.
            tail = order->prev;
        }

        // Clean up the removed order's pointers.
        order->next = nullptr;
        order->prev = nullptr;
    }
};

// --- THE ZERO-ALLOCATION LIMIT ORDER BOOK ---
template <std::size_t MaxOrders, std::size_t MaxPriceLevels>
class LimitOrderBook {
    private:
        // All orders will live in the ObjectPool memory arena
        tradeforge::memory::ObjectPool<Order, MaxOrders> order_pool_;

        // The dense arrays representing the Bids (buyers) and Asks (sellers).
        std::array<PriceLevel, MaxPriceLevels> bids_;
        std::array<PriceLevel, MaxPriceLevels> asks_;

    public:
        LimitOrderBook() = default;

        // Handles an incoming Tick to add a new Order.
        // Returns true upon successfully initializing a new Order from the Tick
        // and pushing it back onto the correct bids_/asks_ array.
        // Returns false if the underlying ObjectPool is out of memory.
        bool add_order(const Tick& tick) noexcept {
            // Grab pre-allocated memory from the hot-path pool
            Order* new_order = order_pool_.acquire();

            // If the pool is dry, we drop the order.
            // In a real system, fallback logic would be required.
            if (!new_order) return false;

            // Initialize the order with the Tick data.
            new_order->initialize(
                tick.order_id, tick.price, 
                tick.quantity, tick.side
            );

            // Push it into the correct price level's intrusive list.
            // Assume tick.price has been normalized to fit inside MaxPriceLevels.
            if (tick.side == Side::Buy) {
                bids_[tick.price].push_back(new_order);
            } else {
                asks_[tick.price].push_back(new_order);
            }

            return true;
        }

        void cancel_order(Order* order) noexcept {
            if (!order) return;

            // Unlink from the intrusive lists in O(1) time.
            if (order->side == Side::Buy) {
                bids_[order->price].remove(order);
            } else {
                asks_[order->price].remove(order);
            }

            // Return the memory to the pool in O(1) time.
            order_pool_.release(order);
        }
};

} // namespace tradeforge::trading