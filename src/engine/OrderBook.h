#pragma once

#include "engine/PriceLevel.h"
#include <map>
#include <unordered_map>
#include <optional>

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

    // Modifies the quantity of an order in the book (useful for partial fills)
    void reduceOrderQuantity(OrderId id, Quantity fillQuantity) {
        auto locIt = orderLocations_.find(id);
        if (locIt != orderLocations_.end()) {
            locIt->second.iterator->quantity -= fillQuantity;
            
            // Also need to update the total quantity in the price level.
            // For this baseline, we just manually adjust it or we could provide a method on PriceLevel.
            // Let's add a method on PriceLevel to handle partial fills if needed, but since we 
            // process fills directly in MatchingEngine, we will handle quantity reduction there 
            // and only remove from book when fully filled.
        }
    }

    std::map<Price, PriceLevel, std::greater<Price>>& getBids() { return bids_; }
    const std::map<Price, PriceLevel, std::greater<Price>>& getBids() const { return bids_; }
    std::map<Price, PriceLevel, std::less<Price>>& getAsks() { return asks_; }
    const std::map<Price, PriceLevel, std::less<Price>>& getAsks() const { return asks_; }

    void removeOrderLocation(OrderId id) {
        orderLocations_.erase(id);
    }
    
    // Removes an empty price level. Should be called by MatchingEngine after full fills exhaust a level.
    void removeEmptyBid(Price price) {
        auto it = bids_.find(price);
        if (it != bids_.end() && it->second.isEmpty()) {
            bids_.erase(it);
        }
    }

    void removeEmptyAsk(Price price) {
        auto it = asks_.find(price);
        if (it != asks_.end() && it->second.isEmpty()) {
            asks_.erase(it);
        }
    }

private:
    std::map<Price, PriceLevel, std::greater<Price>> bids_; // Highest price first
    std::map<Price, PriceLevel, std::less<Price>> asks_;    // Lowest price first
    
    std::unordered_map<OrderId, OrderLocation> orderLocations_;
};

} // namespace engine
