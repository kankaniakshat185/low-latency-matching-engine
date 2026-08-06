#pragma once

#include "engine/Order.h"
#include "engine/PriceLevel.h"
#include "engine/Trade.h"
#include "engine/Types.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine {

// This is "1.0" / "the baseline" in the Phase 4 comparative study —
// std::map + std::list + std::unordered_map, unchanged since Phase 1.
// Deliberately never renamed to OrderBookV1 when OrderBookV2/V3 were added
// (src/structures/): renaming would have touched every existing test, doc,
// and ADR reference to "OrderBook" for a purely cosmetic reason. The
// version mapping lives in one place instead: MatchingEngine.h's
// `using MatchingEngine = MatchingEngineT<OrderBook>;`.
// See public_docs/optimization_history.md and the ADR log for the other
// variants and the measured comparison between all of them.
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
    [[nodiscard]] bool hasOrder(OrderId id) const { return orderLocations_.find(id) != orderLocations_.end(); }

    // Cancels an order by ID. Returns true if successful, false if not found.
    [[nodiscard]] bool cancelOrder(OrderId id) {
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

    // Matches `order` against the resting orders on the *opposite* side of
    // the book (asks if order.side == Buy, bids if Sell), respecting strict
    // price-time priority. If `limitPrice` holds a value, only resting
    // orders priced at or better than it are matched (limit-order
    // semantics: stop once the book crosses beyond the limit); if empty,
    // the book is swept unconditionally until either side is exhausted
    // (market-order semantics).
    //
    // Mutates `order.quantity` down as it fills, appends one Trade per fill
    // to `trades`, and removes any resting order that becomes fully filled.
    // Does NOT rest `order`'s remainder in the book on its own — the caller
    // (MatchingEngine) decides whether an unfilled remainder should join
    // the book (limit orders) or be discarded (market orders).
    //
    // This owns the traversal/mutation that used to be duplicated four
    // times (once each for matchLimitBuy/Sell and matchMarketBuy/Sell) in
    // MatchingEngine — living here means it can touch bids_/asks_/
    // orderLocations_ directly instead of needing mutable public accessors.
    void matchAgainst(Order& order, std::optional<Price> limitPrice, std::vector<Trade>& trades) {
        if (order.side == Side::Buy) {
            matchAgainstSide(order, asks_, /*isBuy=*/true, limitPrice, trades);
        } else {
            matchAgainstSide(order, bids_, /*isBuy=*/false, limitPrice, trades);
        }
    }

    // Read-only only: nothing outside OrderBook needs (or should have)
    // mutable access to the price-level maps. addOrder/cancelOrder/
    // matchAgainst all mutate bids_/asks_ directly as private members.
    const std::map<Price, PriceLevel, std::greater<Price>>& getBids() const { return bids_; }
    const std::map<Price, PriceLevel, std::less<Price>>& getAsks() const { return asks_; }

private:
    // Shared implementation for both sides of the book: bids_ and asks_ are
    // different concrete map types (opposite comparators), but both expose
    // the same iterator/erase interface, so one template covers both.
    template <typename PriceLevelMap>
    void matchAgainstSide(Order& order, PriceLevelMap& levels, bool isBuy, std::optional<Price> limitPrice,
                          std::vector<Trade>& trades) {
        auto priceLevelIt = levels.begin();
        while (priceLevelIt != levels.end() && order.quantity > 0) {
            if (limitPrice) {
                Price levelPrice = priceLevelIt->first;
                // Buy: can't pay more than the limit. Sell: won't accept less than it.
                bool crossed = isBuy ? (levelPrice > *limitPrice) : (levelPrice < *limitPrice);
                if (crossed)
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
                    orderLocations_.erase(restingOrder.id);
                    orderIt = orders.erase(orderIt);
                } else {
                    // Resting order is partially filled; incoming order is
                    // fully consumed (the outer while() will now exit).
                    break;
                }
            }

            if (level.isEmpty()) {
                priceLevelIt = levels.erase(priceLevelIt);
            } else {
                ++priceLevelIt;
            }
        }
    }

    std::map<Price, PriceLevel, std::greater<Price>> bids_; // Highest price first
    std::map<Price, PriceLevel, std::less<Price>> asks_;    // Lowest price first

    std::unordered_map<OrderId, OrderLocation> orderLocations_;
};

} // namespace engine
