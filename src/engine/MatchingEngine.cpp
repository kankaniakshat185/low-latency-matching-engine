#include "engine/MatchingEngine.h"
#include <cassert>
#include <optional>
#include <stdexcept>
#include <string>

namespace engine {

std::vector<Trade> MatchingEngine::processOrder(Order order) {
    if (order.quantity == 0) {
        throw std::invalid_argument(
            "MatchingEngine::processOrder: order quantity must be greater than zero (id=" +
            std::to_string(order.id) + ")");
    }
    if (book_.hasOrder(order.id)) {
        throw std::invalid_argument(
            "MatchingEngine::processOrder: duplicate OrderId " + std::to_string(order.id) +
            " (already resting in the book)");
    }

    std::vector<Trade> trades;

    // Limit orders may only match at their limit price or better; market
    // orders sweep unconditionally, so pass no limit at all.
    std::optional<Price> limitPrice;
    if (order.type == OrderType::Limit) {
        limitPrice = order.price;
    }

    book_.matchAgainst(order, limitPrice, trades);

    // Only limit orders rest their unfilled remainder in the book; a
    // market order's unfilled remainder is discarded by design.
    if (order.type == OrderType::Limit && order.quantity > 0) {
        // Guaranteed to succeed: the duplicate-id check above already
        // confirmed this id isn't resting anywhere, and matching never
        // introduces a new id collision.
        [[maybe_unused]] bool added = book_.addOrder(order);
        assert(added && "OrderBook::addOrder rejected an id already validated as unique by processOrder");
    }

    return trades;
}

bool MatchingEngine::cancelOrder(OrderId id) {
    return book_.cancelOrder(id);
}

} // namespace engine
