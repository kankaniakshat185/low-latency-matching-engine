#pragma once

#include <cstdint>

namespace engine {

using OrderId = uint64_t;
using Price = uint64_t;    // Using integer representation (e.g., multiplied by 10000) for precision and speed
using Quantity = uint32_t;

enum class Side : uint8_t {
    Buy = 0,
    Sell = 1
};

enum class OrderType : uint8_t {
    Limit = 0,
    Market = 1
};

} // namespace engine
