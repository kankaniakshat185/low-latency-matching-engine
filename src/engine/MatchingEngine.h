#pragma once

#include "engine/OrderBook.h"
#include "engine/Trade.h"
#include <vector>

namespace engine {

class MatchingEngine {
public:
    MatchingEngine() = default;

    // Process a new incoming order. Returns a list of generated trades.
    std::vector<Trade> processOrder(Order order);

    // Cancel an existing order in the book. Returns true if successful.
    bool cancelOrder(OrderId id);

    // Provide const access to the order book for inspection
    const OrderBook& getOrderBook() const { return book_; }

private:
    OrderBook book_;

    std::vector<Trade> matchLimitBuy(Order& order);
    std::vector<Trade> matchLimitSell(Order& order);
    std::vector<Trade> matchMarketBuy(Order& order);
    std::vector<Trade> matchMarketSell(Order& order);
};

} // namespace engine
