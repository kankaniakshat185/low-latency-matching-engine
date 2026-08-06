#pragma once

#include "engine/Order.h"
#include "engine/Trade.h"
#include "engine/Types.h"
#include "structures/OrderPool.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine {
namespace structures {

// 2.0 of the Phase 4 comparative study (see the Phase 4 roadmap and
// ADR-0017). Isolates the cost of per-order heap allocation and
// std::list's node-based cache-unfriendliness, while deliberately leaving
// price levels on std::map, unchanged from the 1.0 baseline
// (engine::OrderBook). If 2.0 beats 1.0 by a lot, that's evidence
// allocation/cache-locality of order storage was the dominant cost; if it
// barely moves, that points at std::map's tree traversal instead — which is
// exactly what 3.0 isolates next.
//
// Every order lives in one OrderPool shared across all price levels,
// linked into per-level intrusive doubly-linked lists via indices.
//
// Public interface intentionally mirrors engine::OrderBook exactly —
// hasOrder/addOrder/cancelOrder/matchAgainst — which is what lets
// MatchingEngineT<BookT> use either one with zero code changes, and what
// lets the differential test harness compare them directly.
class OrderBookV2 {
public:
    // No default capacity on purpose: per ADR-0017, the pool should be
    // sized deliberately for the workload it's about to run, not guessed.
    explicit OrderBookV2(size_t orderPoolCapacity) : pool_(orderPoolCapacity) {}

    [[nodiscard]] bool addOrder(const Order& order) {
        if (orderLocations_.find(order.id) != orderLocations_.end()) {
            return false;
        }
        if (order.side == Side::Buy) {
            auto it = bids_.find(order.price);
            if (it == bids_.end()) {
                it = bids_.emplace(order.price, PriceLevelV2(order.price)).first;
            }
            uint32_t nodeIndex = appendOrder(it->second, order);
            orderLocations_[order.id] = {Side::Buy, order.price, nodeIndex};
        } else {
            auto it = asks_.find(order.price);
            if (it == asks_.end()) {
                it = asks_.emplace(order.price, PriceLevelV2(order.price)).first;
            }
            uint32_t nodeIndex = appendOrder(it->second, order);
            orderLocations_[order.id] = {Side::Sell, order.price, nodeIndex};
        }
        return true;
    }

    [[nodiscard]] bool hasOrder(OrderId id) const { return orderLocations_.find(id) != orderLocations_.end(); }

    [[nodiscard]] bool cancelOrder(OrderId id) {
        auto locIt = orderLocations_.find(id);
        if (locIt == orderLocations_.end()) {
            return false;
        }
        const auto& loc = locIt->second;
        if (loc.side == Side::Buy) {
            auto priceIt = bids_.find(loc.price);
            if (priceIt != bids_.end()) {
                removeOrder(priceIt->second, loc.nodeIndex);
                if (priceIt->second.headIndex == kInvalidNodeIndex) {
                    bids_.erase(priceIt);
                }
            }
        } else {
            auto priceIt = asks_.find(loc.price);
            if (priceIt != asks_.end()) {
                removeOrder(priceIt->second, loc.nodeIndex);
                if (priceIt->second.headIndex == kInvalidNodeIndex) {
                    asks_.erase(priceIt);
                }
            }
        }
        orderLocations_.erase(locIt);
        return true;
    }

    void matchAgainst(Order& order, std::optional<Price> limitPrice, std::vector<Trade>& trades) {
        if (order.side == Side::Buy) {
            matchAgainstSide(order, asks_, /*isBuy=*/true, limitPrice, trades);
        } else {
            matchAgainstSide(order, bids_, /*isBuy=*/false, limitPrice, trades);
        }
    }

private:
    struct PriceLevelV2 {
        explicit PriceLevelV2(Price p) : price(p) {}
        Price price;
        Quantity totalQuantity = 0;
        uint32_t headIndex = kInvalidNodeIndex; // front of the list = oldest = next to fill (time priority)
        uint32_t tailIndex = kInvalidNodeIndex; // back of the list = append point
    };

    struct OrderLocationV2 {
        Side side;
        Price price;
        uint32_t nodeIndex;
    };

    // Mirrors PriceLevel::addOrder (1.0): appends to the tail, adjusts totalQuantity.
    uint32_t appendOrder(PriceLevelV2& level, const Order& order) {
        uint32_t index = pool_.acquire(order);
        OrderNode& node = pool_[index];
        node.prev = level.tailIndex;
        node.next = kInvalidNodeIndex;
        if (level.tailIndex != kInvalidNodeIndex) {
            pool_[level.tailIndex].next = index;
        } else {
            level.headIndex = index; // list was empty
        }
        level.tailIndex = index;
        level.totalQuantity += order.quantity;
        return index;
    }

    // Mirrors PriceLevel::removeOrder (1.0, the cancel path): unlinks,
    // releases the slot, and subtracts the order's current quantity.
    void removeOrder(PriceLevelV2& level, uint32_t nodeIndex) {
        level.totalQuantity -= pool_[nodeIndex].order.quantity;
        unlinkOnly(level, nodeIndex);
    }

    // Mirrors PriceLevel::decreaseQuantity (1.0, the match-path fill
    // accounting): adjusts totalQuantity only, no list mutation.
    void decreaseQuantity(PriceLevelV2& level, Quantity qty) {
        assert(qty <= level.totalQuantity && "OrderBookV2::decreaseQuantity: qty exceeds level's tracked total");
        level.totalQuantity -= qty;
    }

    // Pure list surgery, used inside the match loop after decreaseQuantity()
    // has already accounted for the trade — mirrors 1.0's direct
    // `orders.erase(orderIt)`, which likewise never touches totalQuantity.
    void unlinkOnly(PriceLevelV2& level, uint32_t nodeIndex) {
        OrderNode& node = pool_[nodeIndex];
        if (node.prev != kInvalidNodeIndex) {
            pool_[node.prev].next = node.next;
        } else {
            level.headIndex = node.next;
        }
        if (node.next != kInvalidNodeIndex) {
            pool_[node.next].prev = node.prev;
        } else {
            level.tailIndex = node.prev;
        }
        pool_.release(nodeIndex);
    }

    // Same shape as engine::OrderBook::matchAgainstSide (ADR-0008), walking
    // an intrusive list via pool indices instead of std::list iterators.
    template <typename PriceLevelMapV2>
    void matchAgainstSide(Order& order, PriceLevelMapV2& levels, bool isBuy, std::optional<Price> limitPrice,
                          std::vector<Trade>& trades) {
        auto priceLevelIt = levels.begin();
        while (priceLevelIt != levels.end() && order.quantity > 0) {
            if (limitPrice) {
                Price levelPrice = priceLevelIt->first;
                bool crossed = isBuy ? (levelPrice > *limitPrice) : (levelPrice < *limitPrice);
                if (crossed)
                    break;
            }

            PriceLevelV2& level = priceLevelIt->second;
            uint32_t nodeIndex = level.headIndex;

            while (nodeIndex != kInvalidNodeIndex && order.quantity > 0) {
                OrderNode& restingNode = pool_[nodeIndex];
                Order& restingOrder = restingNode.order;
                Quantity tradeQty = std::min(order.quantity, restingOrder.quantity);
                assert(tradeQty > 0 && tradeQty <= order.quantity && tradeQty <= restingOrder.quantity &&
                       "tradeQty must be a positive amount bounded by both sides' remaining quantity");

                trades.emplace_back(restingOrder.id, order.id, restingOrder.price, tradeQty);

                order.quantity -= tradeQty;
                restingOrder.quantity -= tradeQty;
                decreaseQuantity(level, tradeQty);

                if (restingOrder.quantity == 0) {
                    orderLocations_.erase(restingOrder.id);
                    uint32_t nextIndex = restingNode.next; // capture before unlink invalidates it
                    unlinkOnly(level, nodeIndex);
                    nodeIndex = nextIndex;
                } else {
                    // Resting order partially filled; incoming order is
                    // fully consumed (the outer while() will now exit).
                    break;
                }
            }

            if (level.headIndex == kInvalidNodeIndex) {
                priceLevelIt = levels.erase(priceLevelIt);
            } else {
                ++priceLevelIt;
            }
        }
    }

    std::map<Price, PriceLevelV2, std::greater<Price>> bids_; // Highest price first
    std::map<Price, PriceLevelV2, std::less<Price>> asks_;    // Lowest price first
    std::unordered_map<OrderId, OrderLocationV2> orderLocations_;
    OrderPool pool_;
};

} // namespace structures
} // namespace engine
