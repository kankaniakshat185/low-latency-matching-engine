#pragma once

#include "engine/Order.h"
#include <cassert>
#include <list>

namespace engine {

class PriceLevel {
public:
    explicit PriceLevel(Price price) : price_(price), totalQuantity_(0) {}

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
        // Quantity is unsigned: an out-of-invariant caller would otherwise
        // wrap silently to a huge value instead of failing loudly. tradeQty
        // is always derived from std::min() against a resting order's own
        // quantity, so this should never trip outside of a caller bug.
        assert(qty <= totalQuantity_ && "PriceLevel::decreaseQuantity: qty exceeds the level's tracked total quantity");
        totalQuantity_ -= qty;
    }

    // Read-only view for callers (tests, invariant checks) that only need
    // to inspect resting orders without mutating them.
    const std::list<Order>& getOrders() const { return orders_; }

private:
    // Only OrderBook is allowed to mutate the resting-order queue directly
    // (it owns the traversal/matching loop in OrderBook::matchAgainst).
    // Every other caller — including MatchingEngine — only ever needs
    // read access, which the public const overload above provides.
    friend class OrderBook;
    std::list<Order>& getOrders() { return orders_; }

    Price price_;
    Quantity totalQuantity_;

    // std::list guarantees iterator stability upon insertion and deletion,
    // which is essential for O(1) cancellations using an iterator map.
    std::list<Order> orders_;
};

} // namespace engine
