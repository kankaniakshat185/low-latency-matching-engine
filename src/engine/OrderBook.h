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

    // Adds a limit order to the book
    void addOrder(const Order& order) {
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
