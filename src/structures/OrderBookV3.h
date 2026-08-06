#pragma once

#include "engine/Order.h"
#include "engine/Trade.h"
#include "engine/Types.h"
#include "structures/OrderPool.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {
namespace structures {

// 3.0 of the Phase 4 comparative study (see the Phase 4 roadmap, ADR-0018,
// ADR-0020). Isolates the cost of std::map's tree traversal / cache misses
// on the price-level lookup structure, while carrying forward 2.0's
// intrusive-list + pool allocator for order storage unchanged. If 3.0 beats
// 2.0 by a lot — especially on Random Prices specifically — that's the
// long-standing cache-miss hypothesis finally getting real evidence.
//
// Price levels are a flat, tick-indexed array instead of a red-black tree:
// every possible price in [minPrice, maxPrice] (stepped by tickSize) gets a
// permanently-allocated slot, so there is no dynamic level insertion or
// erasure at all — a deliberate, bounded-price-range assumption (ADR-0020),
// the same kind of "sized deliberately, not guessed" tradeoff as 2.0's pool
// (ADR-0017). An occupancy bitmap plus std::countr_zero/countl_zero
// (C++20 <bit>, not compiler-specific builtins) finds the next/previous
// occupied level in near-O(1) instead of scanning the whole range.
//
// Public interface intentionally mirrors engine::OrderBook and
// OrderBookV2 exactly — hasOrder/addOrder/cancelOrder/matchAgainst.
class OrderBookV3 {
public:
    static constexpr size_t kNoTick = static_cast<size_t>(-1);

    OrderBookV3(Price minPrice, Price maxPrice, Price tickSize, size_t orderPoolCapacity)
        : minPrice_(minPrice),
          tickSize_(tickSize),
          numTicks_(computeNumTicks(minPrice, maxPrice, tickSize)),
          bids_(numTicks_),
          asks_(numTicks_),
          pool_(orderPoolCapacity) {}

    [[nodiscard]] bool addOrder(const Order& order) {
        if (orderLocations_.find(order.id) != orderLocations_.end()) {
            return false;
        }
        size_t tick = tickIndexOrThrow(order.price);
        FlatSide& side = (order.side == Side::Buy) ? bids_ : asks_;
        uint32_t nodeIndex = appendOrder(side, tick, order);
        orderLocations_[order.id] = {order.side, tick, nodeIndex};
        return true;
    }

    [[nodiscard]] bool hasOrder(OrderId id) const { return orderLocations_.find(id) != orderLocations_.end(); }

    [[nodiscard]] bool cancelOrder(OrderId id) {
        auto locIt = orderLocations_.find(id);
        if (locIt == orderLocations_.end()) {
            return false;
        }
        const auto& loc = locIt->second;
        FlatSide& side = (loc.side == Side::Buy) ? bids_ : asks_;
        removeOrder(side, loc.tick, loc.nodeIndex);
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
    struct PriceLevelV3 {
        Quantity totalQuantity = 0;
        uint32_t headIndex = kInvalidNodeIndex; // front of the list = oldest = next to fill (time priority)
        uint32_t tailIndex = kInvalidNodeIndex; // back of the list = append point
    };

    struct OrderLocationV3 {
        Side side;
        size_t tick;
        uint32_t nodeIndex;
    };

    // One side of the book: a flat array of price-level slots, plus a
    // bitmap of which ticks currently have at least one resting order.
    struct FlatSide {
        explicit FlatSide(size_t numTicks) : levels(numTicks), occupancy((numTicks + 63) / 64, 0) {}

        std::vector<PriceLevelV3> levels;
        std::vector<uint64_t> occupancy;

        bool isOccupied(size_t tick) const { return (occupancy[tick / 64] >> (tick % 64)) & 1ULL; }
        void setOccupied(size_t tick) { occupancy[tick / 64] |= (1ULL << (tick % 64)); }
        void clearOccupied(size_t tick) { occupancy[tick / 64] &= ~(1ULL << (tick % 64)); }

        // Lowest occupied tick >= from, or levels.size() if none exist.
        size_t nextOccupiedFrom(size_t from) const {
            size_t numTicks = levels.size();
            if (from >= numTicks) {
                return numTicks;
            }
            size_t wordIdx = from / 64;
            uint64_t word = occupancy[wordIdx] & (~0ULL << (from % 64));
            while (true) {
                if (word != 0) {
                    size_t tick = wordIdx * 64 + static_cast<size_t>(std::countr_zero(word));
                    return tick < numTicks ? tick : numTicks;
                }
                ++wordIdx;
                if (wordIdx >= occupancy.size()) {
                    return numTicks;
                }
                word = occupancy[wordIdx];
            }
        }

        // Highest occupied tick <= from, or kNoTick if none exist.
        // Caller guarantees from < levels.size() (checked at call sites).
        size_t prevOccupiedFrom(size_t from) const {
            size_t wordIdx = from / 64;
            size_t bitInWord = from % 64;
            uint64_t mask = (bitInWord == 63) ? ~0ULL : ((1ULL << (bitInWord + 1)) - 1);
            uint64_t word = occupancy[wordIdx] & mask;
            while (true) {
                if (word != 0) {
                    size_t bit = 63 - static_cast<size_t>(std::countl_zero(word));
                    return wordIdx * 64 + bit;
                }
                if (wordIdx == 0) {
                    return kNoTick;
                }
                --wordIdx;
                word = occupancy[wordIdx];
            }
        }
    };

    static size_t computeNumTicks(Price minPrice, Price maxPrice, Price tickSize) {
        if (tickSize == 0 || maxPrice <= minPrice) {
            throw std::invalid_argument(
                "OrderBookV3: invalid price range or tick size (need maxPrice > minPrice, "
                "tickSize > 0)");
        }
        return static_cast<size_t>((maxPrice - minPrice) / tickSize) + 1;
    }

    size_t tickIndexOrThrow(Price price) const {
        if (price < minPrice_) {
            throw std::out_of_range("OrderBookV3: price " + std::to_string(price) +
                                    " is below the configured minimum " + std::to_string(minPrice_));
        }
        size_t tick = static_cast<size_t>((price - minPrice_) / tickSize_);
        if (tick >= numTicks_) {
            throw std::out_of_range("OrderBookV3: price " + std::to_string(price) + " is above the configured maximum");
        }
        return tick;
    }

    Price priceAtTick(size_t tick) const { return minPrice_ + static_cast<Price>(tick) * tickSize_; }

    // Mirrors OrderBookV2::appendOrder, plus marking the tick occupied the
    // moment its list stops being empty.
    uint32_t appendOrder(FlatSide& side, size_t tick, const Order& order) {
        PriceLevelV3& level = side.levels[tick];
        bool wasEmpty = (level.headIndex == kInvalidNodeIndex);
        uint32_t index = pool_.acquire(order);
        OrderNode& node = pool_[index];
        node.prev = level.tailIndex;
        node.next = kInvalidNodeIndex;
        if (level.tailIndex != kInvalidNodeIndex) {
            pool_[level.tailIndex].next = index;
        } else {
            level.headIndex = index;
        }
        level.tailIndex = index;
        level.totalQuantity += order.quantity;
        if (wasEmpty) {
            side.setOccupied(tick);
        }
        return index;
    }

    // Mirrors OrderBookV2::removeOrder (the cancel path).
    void removeOrder(FlatSide& side, size_t tick, uint32_t nodeIndex) {
        side.levels[tick].totalQuantity -= pool_[nodeIndex].order.quantity;
        unlinkOnly(side, tick, nodeIndex);
    }

    void decreaseQuantity(PriceLevelV3& level, Quantity qty) {
        assert(qty <= level.totalQuantity && "OrderBookV3::decreaseQuantity: qty exceeds level's tracked total");
        level.totalQuantity -= qty;
    }

    // Pure list surgery (mirrors OrderBookV2::unlinkOnly), plus clearing the
    // tick's occupancy bit the moment its list becomes empty.
    void unlinkOnly(FlatSide& side, size_t tick, uint32_t nodeIndex) {
        PriceLevelV3& level = side.levels[tick];
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
        if (level.headIndex == kInvalidNodeIndex) {
            side.clearOccupied(tick);
        }
    }

    // Same shape as OrderBook::matchAgainstSide / OrderBookV2::matchAgainstSide,
    // walking occupied ticks via the bitmap instead of a std::map iterator.
    void matchAgainstSide(Order& order, FlatSide& side, bool isBuy, std::optional<Price> limitPrice,
                          std::vector<Trade>& trades) {
        size_t tick = isBuy ? side.nextOccupiedFrom(0) : side.prevOccupiedFrom(side.levels.size() - 1);
        bool haveLevel = isBuy ? (tick < side.levels.size()) : (tick != kNoTick);

        while (haveLevel && order.quantity > 0) {
            Price levelPrice = priceAtTick(tick);
            if (limitPrice) {
                bool crossed = isBuy ? (levelPrice > *limitPrice) : (levelPrice < *limitPrice);
                if (crossed)
                    break;
            }

            PriceLevelV3& level = side.levels[tick];
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
                    uint32_t nextIndex = restingNode.next;
                    unlinkOnly(side, tick, nodeIndex);
                    nodeIndex = nextIndex;
                } else {
                    // Resting order partially filled; incoming order is
                    // fully consumed (the outer while() will now exit).
                    break;
                }
            }

            if (level.headIndex == kInvalidNodeIndex) {
                if (isBuy) {
                    tick = side.nextOccupiedFrom(tick + 1);
                    haveLevel = (tick < side.levels.size());
                } else if (tick == 0) {
                    haveLevel = false;
                } else {
                    tick = side.prevOccupiedFrom(tick - 1);
                    haveLevel = (tick != kNoTick);
                }
            } else {
                // Level still has resting orders — only happens once the
                // aggressor is exhausted, which the while() already checks.
                break;
            }
        }
    }

    Price minPrice_;
    Price tickSize_;
    size_t numTicks_;
    FlatSide bids_;
    FlatSide asks_;
    std::unordered_map<OrderId, OrderLocationV3> orderLocations_;
    OrderPool pool_;
};

} // namespace structures
} // namespace engine
