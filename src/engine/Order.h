#pragma once

#include "engine/Types.h"

namespace engine {

struct Order {
    OrderId id;
    Price price;      // Irrelevant for Market orders, but included in struct to avoid polymorphism
    Quantity quantity;
    Side side;
    OrderType type;

    Order(OrderId id, Price price, Quantity quantity, Side side, OrderType type)
        : id(id), price(price), quantity(quantity), side(side), type(type) {}
};

} // namespace engine
