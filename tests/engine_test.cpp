#include <gtest/gtest.h>
#include "engine/MatchingEngine.h"
#include "engine/Order.h"
#include "engine/Types.h"
#include <random>
#include <stdexcept>
#include <unordered_set>
#include <vector>

using namespace engine;

class MatchingEngineTest : public ::testing::Test {
protected:
    MatchingEngine engine;

    void SetUp() override {
        // Automatically called before each test
    }

    void TearDown() override {
        // Automatically called after each test
    }
};

TEST_F(MatchingEngineTest, EmptyBookLimitOrder) {
    auto trades = engine.processOrder(Order(1, 100, 10, Side::Buy, OrderType::Limit));
    EXPECT_TRUE(trades.empty());
}

TEST_F(MatchingEngineTest, EmptyBookMarketOrder) {
    auto trades = engine.processOrder(Order(1, 0, 10, Side::Sell, OrderType::Market));
    EXPECT_TRUE(trades.empty());
}

TEST_F(MatchingEngineTest, ExactMatch) {
    auto t1 = engine.processOrder(Order(1, 100, 10, Side::Sell, OrderType::Limit));
    EXPECT_TRUE(t1.empty());

    auto t2 = engine.processOrder(Order(2, 100, 10, Side::Buy, OrderType::Limit));
    ASSERT_EQ(t2.size(), 1);

    EXPECT_EQ(t2[0].makerOrderId, 1);
    EXPECT_EQ(t2[0].takerOrderId, 2);
    EXPECT_EQ(t2[0].quantity, 10);
    EXPECT_EQ(t2[0].price, 100);
}

TEST_F(MatchingEngineTest, PartialFillAggressive) {
    engine.processOrder(Order(1, 100, 5, Side::Sell, OrderType::Limit));

    // Aggressive buy order is larger than resting liquidity
    auto trades = engine.processOrder(Order(2, 100, 10, Side::Buy, OrderType::Limit));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 5);
    EXPECT_EQ(trades[0].price, 100);

    // Remaining 5 qty should be resting on the book. A new sell should match it.
    auto trades2 = engine.processOrder(Order(3, 100, 5, Side::Sell, OrderType::Limit));
    ASSERT_EQ(trades2.size(), 1);
    EXPECT_EQ(trades2[0].makerOrderId, 2);
    EXPECT_EQ(trades2[0].takerOrderId, 3);
}

TEST_F(MatchingEngineTest, PartialFillPassive) {
    engine.processOrder(Order(1, 100, 10, Side::Sell, OrderType::Limit));

    // Aggressive buy order is smaller than resting liquidity
    auto trades = engine.processOrder(Order(2, 100, 5, Side::Buy, OrderType::Limit));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 5);

    // The passive order (1) should still have 5 qty left.
    auto trades2 = engine.processOrder(Order(3, 100, 5, Side::Buy, OrderType::Limit));
    ASSERT_EQ(trades2.size(), 1);
    EXPECT_EQ(trades2[0].makerOrderId, 1);
    EXPECT_EQ(trades2[0].takerOrderId, 3);
}

TEST_F(MatchingEngineTest, PriceTimePriorityFIFO) {
    engine.processOrder(Order(1, 100, 10, Side::Sell, OrderType::Limit));
    engine.processOrder(Order(2, 100, 10, Side::Sell, OrderType::Limit));

    // Market order sweeps 15. Should take all 10 from order 1, and 5 from order 2.
    auto trades = engine.processOrder(Order(3, 0, 15, Side::Buy, OrderType::Market));

    ASSERT_EQ(trades.size(), 2);

    EXPECT_EQ(trades[0].makerOrderId, 1);
    EXPECT_EQ(trades[0].quantity, 10);

    EXPECT_EQ(trades[1].makerOrderId, 2);
    EXPECT_EQ(trades[1].quantity, 5);
}

TEST_F(MatchingEngineTest, BetterPricePriority) {
    engine.processOrder(Order(1, 101, 10, Side::Sell, OrderType::Limit)); // Worse price
    engine.processOrder(Order(2, 100, 10, Side::Sell, OrderType::Limit)); // Better price

    // Buy at 101 should match with 100 first
    auto trades = engine.processOrder(Order(3, 101, 10, Side::Buy, OrderType::Limit));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].makerOrderId, 2);
    EXPECT_EQ(trades[0].price, 100);
}

TEST_F(MatchingEngineTest, CancellationSuccess) {
    engine.processOrder(Order(1, 100, 10, Side::Sell, OrderType::Limit));
    bool success = engine.cancelOrder(1);
    EXPECT_TRUE(success);

    auto trades = engine.processOrder(Order(2, 100, 10, Side::Buy, OrderType::Limit));
    EXPECT_TRUE(trades.empty()); // Order 1 was cancelled, so no match
}

TEST_F(MatchingEngineTest, CancellationFailure) {
    bool success = engine.cancelOrder(999);
    EXPECT_FALSE(success);
}

TEST_F(MatchingEngineTest, CancelAfterFullFillReturnsFalse) {
    engine.processOrder(Order(1, 100, 10, Side::Sell, OrderType::Limit));

    // Fully fill order 1; it should be removed from the book entirely.
    auto trades = engine.processOrder(Order(2, 100, 10, Side::Buy, OrderType::Limit));
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 10);

    // Order 1 no longer rests anywhere; cancelling it must fail cleanly,
    // not crash or silently cancel an unrelated order.
    EXPECT_FALSE(engine.cancelOrder(1));
}

// ---------------------------------------------------------
// Regression tests for validation gaps found in the staff audit
// ---------------------------------------------------------

TEST_F(MatchingEngineTest, ZeroQuantityOrderIsRejected) {
    EXPECT_THROW(engine.processOrder(Order(1, 100, 0, Side::Buy, OrderType::Limit)), std::invalid_argument);
}

TEST_F(MatchingEngineTest, DuplicateOrderIdIsRejectedWhenResting) {
    // Order 1 rests on the book (nothing to match against).
    auto t1 = engine.processOrder(Order(1, 100, 10, Side::Sell, OrderType::Limit));
    EXPECT_TRUE(t1.empty());

    // A second, unrelated order reusing id 1 must be rejected outright,
    // not silently overwrite order 1's entry in the cancel index (the
    // original bug: order 1 would stay physically in the book but become
    // permanently uncancellable).
    EXPECT_THROW(engine.processOrder(Order(1, 101, 5, Side::Sell, OrderType::Limit)), std::invalid_argument);

    // Order 1 must still be exactly as it was: present and cancellable.
    EXPECT_TRUE(engine.cancelOrder(1));
    EXPECT_FALSE(engine.cancelOrder(1)); // now gone, second cancel fails cleanly
}

TEST_F(MatchingEngineTest, DuplicateOrderIdIsRejectedEvenWhenItWouldSelfMatch) {
    // Order 1 rests as a resting Sell.
    engine.processOrder(Order(1, 100, 10, Side::Sell, OrderType::Limit));

    // A Buy carrying the *same* id, aggressive enough to cross, must still
    // be rejected up front rather than matching against (and possibly
    // trading against) its own id.
    EXPECT_THROW(engine.processOrder(Order(1, 100, 10, Side::Buy, OrderType::Limit)), std::invalid_argument);

    // The original resting order must be untouched.
    EXPECT_TRUE(engine.cancelOrder(1));
}

// After a random sequence of inserts (some deliberately reusing live ids,
// which must be rejected) and cancels, the book's invariants must hold:
//   - every price level's tracked total quantity equals the sum of the
//     quantities of the orders actually resting in it
//   - no OrderId appears more than once among resting orders
// This is the single test that would have caught the duplicate-id
// corruption bug on its own.
static void assertBookInvariantsHold(const OrderBook& book) {
    std::unordered_set<OrderId> seenIds;

    auto checkSide = [&](const auto& levels) {
        for (const auto& [price, level] : levels) {
            Quantity summed = 0;
            for (const auto& order : level.getOrders()) {
                summed += order.quantity;
                ASSERT_TRUE(seenIds.insert(order.id).second)
                    << "OrderId " << order.id << " rests more than once in the book";
            }
            ASSERT_EQ(summed, level.getTotalQuantity())
                << "PriceLevel " << price << " totalQuantity_ is out of sync with its resting orders";
        }
    };

    checkSide(book.getBids());
    checkSide(book.getAsks());
}

TEST(MatchingEngineFuzz, RandomOpsWithIdReusePreserveBookInvariants) {
    MatchingEngine fuzzEngine;
    std::mt19937_64 rng(1234);
    std::uniform_int_distribution<Price> priceDist(9000, 11000);
    std::uniform_int_distribution<Quantity> qtyDist(1, 1000);
    std::uniform_int_distribution<int> sideDist(0, 1);
    std::uniform_int_distribution<int> reuseDist(0, 9); // ~10% id reuse attempts

    std::vector<OrderId> liveIds;
    OrderId nextId = 1;

    for (int i = 0; i < 20'000; ++i) {
        bool attemptReuse = !liveIds.empty() && reuseDist(rng) == 0;
        OrderId id;
        bool expectDuplicateRejection = false;

        if (attemptReuse) {
            size_t idx = rng() % liveIds.size();
            id = liveIds[idx];
            // A "live" id may already have been fully matched away by an
            // earlier iteration (it's no longer resting) — reusing it is
            // then perfectly legitimate, not a collision. Only expect a
            // rejection if it's still actually resting in the book.
            expectDuplicateRejection = fuzzEngine.getOrderBook().hasOrder(id);
            if (!expectDuplicateRejection) {
                liveIds[idx] = liveIds.back();
                liveIds.pop_back();
            }
        } else {
            id = nextId++;
        }

        Side side = sideDist(rng) == 0 ? Side::Buy : Side::Sell;
        Order order(id, priceDist(rng), qtyDist(rng), side, OrderType::Limit);

        if (expectDuplicateRejection) {
            EXPECT_THROW(fuzzEngine.processOrder(order), std::invalid_argument);
        } else {
            fuzzEngine.processOrder(order);
            liveIds.push_back(id);
        }

        // Occasionally cancel a live order to exercise churn too.
        if (!liveIds.empty() && reuseDist(rng) == 0) {
            size_t idx = rng() % liveIds.size();
            OrderId cancelId = liveIds[idx];
            if (fuzzEngine.cancelOrder(cancelId)) {
                liveIds[idx] = liveIds.back();
                liveIds.pop_back();
            }
        }

        assertBookInvariantsHold(fuzzEngine.getOrderBook());
    }
}

// A price level with no resting orders left should be erased from the
// book's map, not just left sitting there empty. Nothing in the public
// API behaves differently either way (an empty PriceLevel is silently
// harmless to correctness), which is exactly why this needs its own
// direct check — a leak here would never show up as a wrong trade, only
// as the map growing without bound over a long-running session.
TEST_F(MatchingEngineTest, PriceLevelRemovedFromBookAfterFullCancel) {
    engine.processOrder(Order(1, 100, 10, Side::Sell, OrderType::Limit));
    ASSERT_EQ(engine.getOrderBook().getAsks().count(100), 1u);

    EXPECT_TRUE(engine.cancelOrder(1));
    EXPECT_EQ(engine.getOrderBook().getAsks().count(100), 0u);
}

TEST_F(MatchingEngineTest, PriceLevelRemovedFromBookAfterFullMatch) {
    engine.processOrder(Order(1, 100, 10, Side::Sell, OrderType::Limit));
    ASSERT_EQ(engine.getOrderBook().getAsks().count(100), 1u);

    auto trades = engine.processOrder(Order(2, 100, 10, Side::Buy, OrderType::Limit));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(engine.getOrderBook().getAsks().count(100), 0u);
}

// Cancelling or querying an id that was never inserted at all — not one
// that used to be resting and got filled/cancelled — on an otherwise
// non-empty book. Distinct from CancellationFailure above (that one runs
// on a completely empty book); this checks the lookup itself, not just
// the empty-book path.
TEST_F(MatchingEngineTest, HasOrderAndCancelReturnFalseForIdThatWasNeverInserted) {
    engine.processOrder(Order(1, 100, 10, Side::Sell, OrderType::Limit));
    EXPECT_FALSE(engine.getOrderBook().hasOrder(999));
    EXPECT_FALSE(engine.cancelOrder(999));
    // The real order must be completely unaffected by the failed lookup.
    EXPECT_TRUE(engine.getOrderBook().hasOrder(1));
}

TEST_F(MatchingEngineTest, MarketOrderDiscardRemainder) {
    engine.processOrder(Order(1, 100, 5, Side::Sell, OrderType::Limit));

    // Market buy for 10
    auto trades = engine.processOrder(Order(2, 0, 10, Side::Buy, OrderType::Market));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 5);

    // The remaining 5 of the market order should be discarded. A new sell should NOT match it.
    auto trades2 = engine.processOrder(Order(3, 100, 5, Side::Sell, OrderType::Limit));
    EXPECT_TRUE(trades2.empty());
}

// ---------------------------------------------------------
// Integration Tests
// ---------------------------------------------------------

TEST(MatchingEngineIntegration, ReplaySimulation) {
    MatchingEngine engine;

    // Simulate parsing a file of mixed actions
    std::vector<Order> fileReplay = {
        Order(1, 100, 50, Side::Sell, OrderType::Limit), Order(2, 101, 50, Side::Sell, OrderType::Limit),
        Order(3, 99, 50, Side::Buy, OrderType::Limit), Order(4, 98, 50, Side::Buy, OrderType::Limit)};

    for (const auto& o : fileReplay) {
        engine.processOrder(o);
    }

    // Cancel an order mid-book
    EXPECT_TRUE(engine.cancelOrder(2));

    // Aggressive sweeping market order
    auto trades = engine.processOrder(Order(5, 0, 100, Side::Buy, OrderType::Market));

    // It should match 50 against Order 1 at price 100.
    // Order 2 was cancelled.
    // The remaining 50 of Order 5 is discarded.
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].makerOrderId, 1);
    EXPECT_EQ(trades[0].quantity, 50);
}
