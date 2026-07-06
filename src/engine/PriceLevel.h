#pragma once

#include "engine/Order.h"
#include <list>

namespace engine {

class PriceLevel {
public:
    PriceLevel(Price price) : price_(price), totalQuantity_(0) {}

    // Add an order to the back of the price level (Time Priority)
    std::list<Order>::iterator addOrder(const Order& order) {
        totalQuantity_ += order.quantity;
        orders_.push_back(order);
        return std::prev(orders_.end());
    }

    // Remove an order by its iterator (O(1) time complexity)
    void removeOrder(std::list<Order>::iterator it) {
        totalQuantity_ -= it->quantity;
        orders_.erase(it);
    }

    // Accessors
    Price getPrice() const { return price_; }
    Quantity getTotalQuantity() const { return totalQuantity_; }
    bool isEmpty() const { return orders_.empty(); }
    
    // Reduces the total quantity (called during partial fills)
    void decreaseQuantity(Quantity qty) {
        totalQuantity_ -= qty;
    }
    
    // Allow the MatchingEngine to iterate and modify orders during matching
    std::list<Order>& getOrders() { return orders_; }

private:
    Price price_;
    Quantity totalQuantity_;
    
    // std::list guarantees iterator stability upon insertion and deletion,
    // which is essential for O(1) cancellations using an iterator map.
    std::list<Order> orders_;
};

} // namespace engine
