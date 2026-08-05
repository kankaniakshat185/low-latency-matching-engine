#include "engine/MatchingEngine.h"
#include <algorithm>
#include <cassert>
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

    if (order.type == OrderType::Limit) {
        if (order.side == Side::Buy) {
            return matchLimitBuy(order);
        } else {
            return matchLimitSell(order);
        }
    } else { // Market order
        if (order.side == Side::Buy) {
            return matchMarketBuy(order);
        } else {
            return matchMarketSell(order);
        }
    }
}

bool MatchingEngine::cancelOrder(OrderId id) {
    return book_.cancelOrder(id);
}

std::vector<Trade> MatchingEngine::matchLimitBuy(Order& order) {
    std::vector<Trade> trades;
    auto& asks = book_.getAsks();

    auto priceLevelIt = asks.begin();
    while (priceLevelIt != asks.end() && order.quantity > 0) {
        // If the best ask is higher than the buy limit price, we cannot match
        if (priceLevelIt->first > order.price) {
            break;
        }

        PriceLevel& level = priceLevelIt->second;
        auto& orders = level.getOrders();
        
        auto orderIt = orders.begin();
        while (orderIt != orders.end() && order.quantity > 0) {
            Order& restingOrder = *orderIt;
            Quantity tradeQty = std::min(order.quantity, restingOrder.quantity);
            assert(tradeQty > 0 && tradeQty <= order.quantity && tradeQty <= restingOrder.quantity &&
                   "tradeQty must be a positive amount bounded by both sides' remaining quantity");

            trades.emplace_back(restingOrder.id, order.id, restingOrder.price, tradeQty);

            order.quantity -= tradeQty;
            restingOrder.quantity -= tradeQty;
            level.decreaseQuantity(tradeQty);

            if (restingOrder.quantity == 0) {
                // Remove from order locations
                book_.removeOrderLocation(restingOrder.id);
                // Remove from the queue
                orderIt = orders.erase(orderIt);
            } else {
                // Resting order is partially filled, no more incoming quantity
                break;
            }
        }

        if (level.isEmpty()) {
            priceLevelIt = asks.erase(priceLevelIt);
        } else {
            ++priceLevelIt;
        }
    }

    if (order.quantity > 0) {
        // Guaranteed to succeed: processOrder() rejected duplicate ids up
        // front, and matching never introduces a new id collision.
        [[maybe_unused]] bool added = book_.addOrder(order);
        assert(added && "OrderBook::addOrder rejected an id already validated as unique by processOrder");
    }

    return trades;
}

std::vector<Trade> MatchingEngine::matchLimitSell(Order& order) {
    std::vector<Trade> trades;
    auto& bids = book_.getBids();

    auto priceLevelIt = bids.begin();
    while (priceLevelIt != bids.end() && order.quantity > 0) {
        // If the best bid is lower than the sell limit price, we cannot match
        if (priceLevelIt->first < order.price) {
            break;
        }

        PriceLevel& level = priceLevelIt->second;
        auto& orders = level.getOrders();
        
        auto orderIt = orders.begin();
        while (orderIt != orders.end() && order.quantity > 0) {
            Order& restingOrder = *orderIt;
            Quantity tradeQty = std::min(order.quantity, restingOrder.quantity);
            assert(tradeQty > 0 && tradeQty <= order.quantity && tradeQty <= restingOrder.quantity &&
                   "tradeQty must be a positive amount bounded by both sides' remaining quantity");

            trades.emplace_back(restingOrder.id, order.id, restingOrder.price, tradeQty);

            order.quantity -= tradeQty;
            restingOrder.quantity -= tradeQty;
            level.decreaseQuantity(tradeQty);

            if (restingOrder.quantity == 0) {
                book_.removeOrderLocation(restingOrder.id);
                orderIt = orders.erase(orderIt);
            } else {
                break;
            }
        }

        if (level.isEmpty()) {
            priceLevelIt = bids.erase(priceLevelIt);
        } else {
            ++priceLevelIt;
        }
    }

    if (order.quantity > 0) {
        // Guaranteed to succeed: processOrder() rejected duplicate ids up
        // front, and matching never introduces a new id collision.
        [[maybe_unused]] bool added = book_.addOrder(order);
        assert(added && "OrderBook::addOrder rejected an id already validated as unique by processOrder");
    }

    return trades;
}

std::vector<Trade> MatchingEngine::matchMarketBuy(Order& order) {
    std::vector<Trade> trades;
    auto& asks = book_.getAsks();

    auto priceLevelIt = asks.begin();
    while (priceLevelIt != asks.end() && order.quantity > 0) {
        PriceLevel& level = priceLevelIt->second;
        auto& orders = level.getOrders();
        
        auto orderIt = orders.begin();
        while (orderIt != orders.end() && order.quantity > 0) {
            Order& restingOrder = *orderIt;
            Quantity tradeQty = std::min(order.quantity, restingOrder.quantity);
            assert(tradeQty > 0 && tradeQty <= order.quantity && tradeQty <= restingOrder.quantity &&
                   "tradeQty must be a positive amount bounded by both sides' remaining quantity");

            trades.emplace_back(restingOrder.id, order.id, restingOrder.price, tradeQty);

            order.quantity -= tradeQty;
            restingOrder.quantity -= tradeQty;
            level.decreaseQuantity(tradeQty);

            if (restingOrder.quantity == 0) {
                book_.removeOrderLocation(restingOrder.id);
                orderIt = orders.erase(orderIt);
            } else {
                break;
            }
        }

        if (level.isEmpty()) {
            priceLevelIt = asks.erase(priceLevelIt);
        } else {
            ++priceLevelIt;
        }
    }

    // Unfilled portion of market order is discarded.
    return trades;
}

std::vector<Trade> MatchingEngine::matchMarketSell(Order& order) {
    std::vector<Trade> trades;
    auto& bids = book_.getBids();

    auto priceLevelIt = bids.begin();
    while (priceLevelIt != bids.end() && order.quantity > 0) {
        PriceLevel& level = priceLevelIt->second;
        auto& orders = level.getOrders();
        
        auto orderIt = orders.begin();
        while (orderIt != orders.end() && order.quantity > 0) {
            Order& restingOrder = *orderIt;
            Quantity tradeQty = std::min(order.quantity, restingOrder.quantity);
            assert(tradeQty > 0 && tradeQty <= order.quantity && tradeQty <= restingOrder.quantity &&
                   "tradeQty must be a positive amount bounded by both sides' remaining quantity");

            trades.emplace_back(restingOrder.id, order.id, restingOrder.price, tradeQty);

            order.quantity -= tradeQty;
            restingOrder.quantity -= tradeQty;
            level.decreaseQuantity(tradeQty);

            if (restingOrder.quantity == 0) {
                book_.removeOrderLocation(restingOrder.id);
                orderIt = orders.erase(orderIt);
            } else {
                break;
            }
        }

        if (level.isEmpty()) {
            priceLevelIt = bids.erase(priceLevelIt);
        } else {
            ++priceLevelIt;
        }
    }

    // Unfilled portion of market order is discarded.
    return trades;
}

} // namespace engine
