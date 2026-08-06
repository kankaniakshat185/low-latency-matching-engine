#pragma once

#include "engine/Order.h"
#include "engine/Trade.h"
#include "engine/Types.h"
#include "structures/OrderPool.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine {
namespace structures {

// 4.0 of the Phase 4 comparative study (see the Phase 4 roadmap, ADR-0021,
// ADR-0022). Two independent changes on top of 3.0, bundled into one
// version because both close gaps 3.0's own ADR named but didn't build:
//
//   1. A cached best-price tick per side. 3.0's matchAgainstSide scans the
//      occupancy bitmap from the array's edge on *every* call, even though
//      the best price rarely moves between consecutive incoming orders.
//      That scan-from-edge is the entire mechanism behind 3.0's Worst Case
//      regression (ADR-0021) — cheap when many levels are occupied near
//      the edge, expensive when the one occupied level sits mid-range.
//      Here, bestBidTick_/bestAskTick_ (kept per-FlatSide as `bestTick`)
//      are updated in O(1) the moment a tick transitions empty<->occupied,
//      so matchAgainstSide starts from the cached value directly and only
//      pays for a bitmap scan on the (comparatively rare) event that the
//      current best level itself empties out.
//
//   2. A flat, OrderId-indexed cancellation index. 1.0/2.0/3.0 all use
//      std::unordered_map<OrderId, OrderLocation> — a hash + bucket walk
//      on every insert/cancel/lookup. Real exchanges assign OrderId as a
//      dense, monotonically increasing sequence number at the gateway, so
//      — the same bounded-domain trade a hard cap for O(1) indexing as
//      3.0's price range (ADR-0020) — this version replaces the hash map
//      with a flat vector<OrderLocationV4> indexed directly by id.
//
// Public interface intentionally still mirrors OrderBook/V2/V3 exactly.
class OrderBookV4 {
public:
    static constexpr size_t kNoTick = static_cast<size_t>(-1);

    OrderBookV4(Price minPrice, Price maxPrice, Price tickSize, size_t orderPoolCapacity, size_t orderIdCapacity)
        : minPrice_(minPrice),
          tickSize_(tickSize),
          numTicks_(computeNumTicks(minPrice, maxPrice, tickSize)),
          bids_(numTicks_, /*isBid=*/true),
          asks_(numTicks_, /*isBid=*/false),
          pool_(orderPoolCapacity),
          orderLocations_(orderIdCapacity) {}

    // Throws std::out_of_range if order.id >= the configured orderIdCapacity
    // — a caller/config violation, same class of error as an out-of-range
    // price (tickIndexOrThrow below), not a routine outcome.
    [[nodiscard]] bool addOrder(const Order& order) {
        size_t idIdx = orderIdIndexOrThrow(order.id);
        if (orderLocations_[idIdx].nodeIndex != kInvalidNodeIndex) {
            return false; // duplicate id — same policy as OrderBook/V2/V3
        }
        size_t tick = tickIndexOrThrow(order.price);
        FlatSide& side = (order.side == Side::Buy) ? bids_ : asks_;
        uint32_t nodeIndex = appendOrder(side, tick, order);
        orderLocations_[idIdx] = {order.side, tick, nodeIndex};
        return true;
    }

    // Out-of-range ids are simply "not present" here, not exceptional —
    // hasOrder/cancelOrder are routine queries, unlike addOrder inserting a
    // new id outside the configured range.
    [[nodiscard]] bool hasOrder(OrderId id) const {
        return id < orderLocations_.size() && orderLocations_[static_cast<size_t>(id)].nodeIndex != kInvalidNodeIndex;
    }

    [[nodiscard]] bool cancelOrder(OrderId id) {
        if (id >= orderLocations_.size()) {
            return false;
        }
        OrderLocationV4& loc = orderLocations_[static_cast<size_t>(id)];
        if (loc.nodeIndex == kInvalidNodeIndex) {
            return false;
        }
        FlatSide& side = (loc.side == Side::Buy) ? bids_ : asks_;
        removeOrder(side, loc.tick, loc.nodeIndex);
        loc.nodeIndex = kInvalidNodeIndex;
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
    struct PriceLevelV4 {
        Quantity totalQuantity = 0;
        uint32_t headIndex = kInvalidNodeIndex; // front of the list = oldest = next to fill (time priority)
        uint32_t tailIndex = kInvalidNodeIndex; // back of the list = append point
    };

    struct OrderLocationV4 {
        Side side = Side::Buy;
        size_t tick = 0;
        uint32_t nodeIndex = kInvalidNodeIndex; // kInvalidNodeIndex doubles as "this id slot is unused"
    };

    // One side of the book: a flat array of price-level slots, an occupancy
    // bitmap (as in 3.0), plus one cached "best" tick (new in 4.0) that's
    // kept correct incrementally instead of recomputed by scanning.
    struct FlatSide {
        FlatSide(size_t numTicks, bool isBid) : levels(numTicks), occupancy((numTicks + 63) / 64, 0), isBid(isBid) {}

        std::vector<PriceLevelV4> levels;
        std::vector<uint64_t> occupancy;
        bool isBid;
        size_t bestTick = kNoTick; // kNoTick means "this side is currently empty"

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
        // Caller guarantees from < levels.size().
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

        // Call the instant `tick` transitions empty -> occupied. O(1): bids
        // want the max occupied tick, asks want the min, so a new tick only
        // ever needs a single comparison against the current best, never a
        // scan.
        void noteInserted(size_t tick) {
            if (bestTick == kNoTick || (isBid ? tick > bestTick : tick < bestTick)) {
                bestTick = tick;
            }
        }

        // Call the instant `tick` transitions occupied -> empty. No-op
        // unless `tick` was the cached best — that's the only case where
        // the cache goes stale, and it's the only case allowed to pay for
        // a bitmap scan to re-establish the new best.
        void noteMaybeBestCleared(size_t tick) {
            if (tick != bestTick) {
                return;
            }
            if (isBid) {
                bestTick = (tick == 0) ? kNoTick : prevOccupiedFrom(tick - 1);
            } else {
                size_t numTicks = levels.size();
                size_t next = (tick + 1 < numTicks) ? nextOccupiedFrom(tick + 1) : numTicks;
                bestTick = (next < numTicks) ? next : kNoTick;
            }
        }
    };

    static size_t computeNumTicks(Price minPrice, Price maxPrice, Price tickSize) {
        if (tickSize == 0 || maxPrice <= minPrice) {
            throw std::invalid_argument(
                "OrderBookV4: invalid price range or tick size (need maxPrice > minPrice, "
                "tickSize > 0)");
        }
        return static_cast<size_t>((maxPrice - minPrice) / tickSize) + 1;
    }

    size_t tickIndexOrThrow(Price price) const {
        if (price < minPrice_) {
            throw std::out_of_range("OrderBookV4: price " + std::to_string(price) +
                                    " is below the configured minimum " + std::to_string(minPrice_));
        }
        size_t tick = static_cast<size_t>((price - minPrice_) / tickSize_);
        if (tick >= numTicks_) {
            throw std::out_of_range("OrderBookV4: price " + std::to_string(price) + " is above the configured maximum");
        }
        return tick;
    }

    size_t orderIdIndexOrThrow(OrderId id) const {
        if (id >= orderLocations_.size()) {
            throw std::out_of_range("OrderBookV4: order id " + std::to_string(id) +
                                    " is >= the configured orderIdCapacity (" + std::to_string(orderLocations_.size()) +
                                    ")");
        }
        return static_cast<size_t>(id);
    }

    Price priceAtTick(size_t tick) const { return minPrice_ + static_cast<Price>(tick) * tickSize_; }

    // Mirrors OrderBookV3::appendOrder, plus updating the side's best-tick
    // cache the moment its list stops being empty.
    uint32_t appendOrder(FlatSide& side, size_t tick, const Order& order) {
        PriceLevelV4& level = side.levels[tick];
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
            side.noteInserted(tick);
        }
        return index;
    }

    // Mirrors OrderBookV3::removeOrder (the cancel path).
    void removeOrder(FlatSide& side, size_t tick, uint32_t nodeIndex) {
        side.levels[tick].totalQuantity -= pool_[nodeIndex].order.quantity;
        unlinkOnly(side, tick, nodeIndex);
    }

    void decreaseQuantity(PriceLevelV4& level, Quantity qty) {
        assert(qty <= level.totalQuantity && "OrderBookV4::decreaseQuantity: qty exceeds level's tracked total");
        level.totalQuantity -= qty;
    }

    // Pure list surgery (mirrors OrderBookV3::unlinkOnly), plus clearing the
    // tick's occupancy bit — and, new in 4.0, refreshing the best-tick cache
    // — the moment its list becomes empty.
    void unlinkOnly(FlatSide& side, size_t tick, uint32_t nodeIndex) {
        PriceLevelV4& level = side.levels[tick];
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
            side.noteMaybeBestCleared(tick);
        }
    }

    // Same shape as OrderBookV3::matchAgainstSide, except the traversal
    // starts from side.bestTick (kept correct by appendOrder/unlinkOnly
    // above) instead of scanning the bitmap from the array's edge.
    void matchAgainstSide(Order& order, FlatSide& side, bool isBuy, std::optional<Price> limitPrice,
                          std::vector<Trade>& trades) {
        size_t tick = side.bestTick;
        bool haveLevel = (tick != kNoTick);

        while (haveLevel && order.quantity > 0) {
            Price levelPrice = priceAtTick(tick);
            if (limitPrice) {
                bool crossed = isBuy ? (levelPrice > *limitPrice) : (levelPrice < *limitPrice);
                if (crossed)
                    break;
            }

            PriceLevelV4& level = side.levels[tick];
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
                    orderLocations_[static_cast<size_t>(restingOrder.id)].nodeIndex = kInvalidNodeIndex;
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
                // unlinkOnly() already refreshed side.bestTick above (it's
                // called for every fully-filled resting order, including
                // the one that just emptied this level) — just re-read it.
                tick = side.bestTick;
                haveLevel = (tick != kNoTick);
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
    OrderPool pool_;
    std::vector<OrderLocationV4> orderLocations_; // indexed directly by OrderId
};

} // namespace structures
} // namespace engine
