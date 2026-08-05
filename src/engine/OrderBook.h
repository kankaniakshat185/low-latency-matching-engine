#pragma once

#include "engine/PriceLevel.h"
#include <map>
#include <unordered_map>

namespace engine {

struct OrderLocation {
    Side side;
    Price price;
    std::list<Order>::iterator iterator;
};

class OrderBook {
public:
    OrderBook() = default;

    // Adds a limit order to the book. Returns false (and leaves the book
    // untouched) if `order.id` already belongs to a resting order.
    //
    // Without this check, a duplicate id would silently overwrite the
    // earlier order's entry in orderLocations_: the earlier order stays
    // physically in its PriceLevel's list (so it still trades normally when
    // swept) but becomes permanently unreachable by id — any later
    // cancelOrder(id) cancels the *new* order instead, and the original can
    // never be cancelled again. No exception, no crash — just a silently
    // corrupted cancel index. MatchingEngine::processOrder additionally
    // rejects duplicate ids up front (via hasOrder) before any matching
    // happens; this check is defense-in-depth for any other caller.
    [[nodiscard]] bool addOrder(const Order& order) {
        if (orderLocations_.find(order.id) != orderLocations_.end()) {
            return false;
        }

        if (order.side == Side::Buy) {
            auto it = bids_.find(order.price);
            if (it == bids_.end()) {
                it = bids_.emplace(order.price, PriceLevel(order.price)).first;
            }
            auto listIt = it->second.addOrder(order);
            orderLocations_[order.id] = {Side::Buy, order.price, listIt};
        } else {
            auto it = asks_.find(order.price);
            if (it == asks_.end()) {
                it = asks_.emplace(order.price, PriceLevel(order.price)).first;
            }
            auto listIt = it->second.addOrder(order);
            orderLocations_[order.id] = {Side::Sell, order.price, listIt};
        }
        return true;
    }

    // True if `id` currently belongs to a resting (unfilled) order in the book.
    bool hasOrder(OrderId id) const {
        return orderLocations_.find(id) != orderLocations_.end();
    }

    // Cancels an order by ID. Returns true if successful, false if not found.
    bool cancelOrder(OrderId id) {
        auto locIt = orderLocations_.find(id);
        if (locIt == orderLocations_.end()) {
            return false;
        }

        const auto& loc = locIt->second;
        if (loc.side == Side::Buy) {
            auto priceIt = bids_.find(loc.price);
            if (priceIt != bids_.end()) {
                priceIt->second.removeOrder(loc.iterator);
                if (priceIt->second.isEmpty()) {
                    bids_.erase(priceIt);
                }
            }
        } else {
            auto priceIt = asks_.find(loc.price);
            if (priceIt != asks_.end()) {
                priceIt->second.removeOrder(loc.iterator);
                if (priceIt->second.isEmpty()) {
                    asks_.erase(priceIt);
                }
            }
        }
        orderLocations_.erase(locIt);
        return true;
    }

    std::map<Price, PriceLevel, std::greater<Price>>& getBids() { return bids_; }
    const std::map<Price, PriceLevel, std::greater<Price>>& getBids() const { return bids_; }
    std::map<Price, PriceLevel, std::less<Price>>& getAsks() { return asks_; }
    const std::map<Price, PriceLevel, std::less<Price>>& getAsks() const { return asks_; }

    void removeOrderLocation(OrderId id) {
        orderLocations_.erase(id);
    }

private:
    std::map<Price, PriceLevel, std::greater<Price>> bids_; // Highest price first
    std::map<Price, PriceLevel, std::less<Price>> asks_;    // Lowest price first
    
    std::unordered_map<OrderId, OrderLocation> orderLocations_;
};

} // namespace engine
