#pragma once

#include <cstdint>

namespace tradeforge::trading {

    // --- FUNDAMENTAL TYPES ---
    // We use strict width integers to guarantee exact byte sizes 
    // across all platforms.

    // We store prices as fixed-point integers to prevent 
    // floating-point errors and slower arithmetic.
    using Price = std::uint64_t;

    // Quantities (number of shares/contracts).
    using Quantity = std::uint32_t;

    // Unique identifier assigned to every order by the exchange.
    using OrderId = std::uint64_t;

    // An enum class backed by a single byte to save memory.
    enum class Side : std::uint8_t {
        Buy = 0,
        Sell = 1
    };

    // --- MARKET DATA STRUCTURES ---

    // A Tick represents an incoming message from the network/exchange.
    // This is the raw data that will flow through the SPSCQueue.
    struct Tick {
        OrderId order_id;   // 8 bytes (total: 8 bytes, aligned to 8)
        Price price;        // 8 bytes (total: 16 bytes, aligned to 8)
        Quantity quantity;  // 4 bytes (total: 20 bytes, aligned to 4)
        Side side;          // 1 byte  (total: 21 bytes, aligned to 1)
        // The compiler will add 3 bytes of padding to align the struct
        // to its members' strictest alignment requirement of 8 bytes.
        // 24 bytes in total.
    };

    // An Order represents a resting limit order inside the order book.
    // This is the object that will be managed by the ObjectPool.
    struct Order {
        OrderId order_id; 
        Price price;      
        Quantity quantity;     
        Side side;    
        
        // Intrusive linked-list pointers.
        // Instead of an std::list<Order>, which would allocate a
        // separate node wrapping each new Order, the Order itself 
        // includes the list-traversal pointers. Required to
        // guarantee a zero-allocation data structure.
        Order* prev = nullptr;
        Order* next = nullptr;


        // Helper to cleanly initialize an Order pulled from the ObjectPool.
        void initialize(
            OrderId id,
            Price p,
            Quantity q,
            Side s
        ) noexcept
        {
            order_id = id;
            price = p;
            quantity = q;
            side = s;
            prev = nullptr;
            next = nullptr;
        }

};

}