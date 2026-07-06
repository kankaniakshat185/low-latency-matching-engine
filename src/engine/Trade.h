#pragma once

#include "engine/Types.h"

namespace engine {

struct Trade {
    OrderId makerOrderId;
    OrderId takerOrderId;
    Price price;
    Quantity quantity;

    Trade(OrderId maker, OrderId taker, Price p, Quantity q)
        : makerOrderId(maker), takerOrderId(taker), price(p), quantity(q) {}
};

} // namespace engine
