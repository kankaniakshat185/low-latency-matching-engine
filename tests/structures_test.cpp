// Direct unit tests for the Phase 4 data structures (OrderPool,
// OrderBookV2/V3/V4) in isolation, not routed through MatchingEngine.
//
// The differential test suite (differential_test.cpp) is excellent at
// proving these variants agree with 1.0 on *valid* input — it replays the
// same randomized action sequence through each and checks the resulting
// trade ledgers match. What it can't exercise is each variant's own
// defensive/boundary behavior: none of its generated workloads ever insert
// a duplicate id, request a price outside a configured range, request an
// out-of-range OrderId, or exhaust a pool's capacity, because doing any of
// those on purpose would just throw and abort the run before a ledger could
// be compared. That gap showed up concretely in a coverage pass: every
// throw/early-return validation branch in OrderPool/OrderBookV3/OrderBookV4
// had zero coverage. These tests close that gap directly.
//
// engine_test.cpp already covers this same class of behavior for 1.0
// (OrderBook) via MatchingEngine — ZeroQuantityOrderIsRejected,
// DuplicateOrderIdIsRejectedWhenResting, etc. This file is the equivalent
// coverage for the variants that engine_test.cpp doesn't touch, plus
// OrderBook's own defense-in-depth duplicate-id guard, which MatchingEngine
// never actually reaches in practice (it checks hasOrder() first) and so
// was previously untested by anything at all.

#include <gtest/gtest.h>
#include "engine/OrderBook.h"
#include "engine/Order.h"
#include "structures/OrderBookV2.h"
#include "structures/OrderBookV3.h"
#include "structures/OrderBookV4.h"
#include "structures/OrderPool.h"
#include <optional>
#include <stdexcept>
#include <vector>

using namespace engine;
using namespace engine::structures;

namespace {
Order makeOrder(OrderId id, Price price, Quantity qty = 100, Side side = Side::Buy) {
    return Order(id, price, qty, side, OrderType::Limit);
}
} // namespace

// ---------------------------------------------------------------------
// OrderPool: fixed-capacity, reject-on-exhaustion (ADR-0017)
// ---------------------------------------------------------------------

TEST(OrderPoolTest, AcquireBeyondCapacityThrows) {
    OrderPool pool(2);
    (void)pool.acquire(makeOrder(1, 100));
    (void)pool.acquire(makeOrder(2, 100));
    EXPECT_THROW((void)pool.acquire(makeOrder(3, 100)), std::runtime_error);
}

TEST(OrderPoolTest, ReleasedSlotIsReusableWithinCapacity) {
    OrderPool pool(1);
    uint32_t index = pool.acquire(makeOrder(1, 100));
    pool.release(index);
    // Capacity is still 1, but the slot was freed — this must not throw.
    uint32_t reused = pool.acquire(makeOrder(2, 200));
    EXPECT_EQ(reused, index);
}

// ---------------------------------------------------------------------
// OrderBook (1.0): the defense-in-depth duplicate guard MatchingEngine
// never actually reaches, tested directly for once.
// ---------------------------------------------------------------------

TEST(OrderBookDirectTest, DuplicateOrderIdRejected) {
    OrderBook book;
    EXPECT_TRUE(book.addOrder(makeOrder(1, 100)));
    EXPECT_FALSE(book.addOrder(makeOrder(1, 200))); // same id, different price — still rejected
    EXPECT_TRUE(book.hasOrder(1));
}

// ---------------------------------------------------------------------
// OrderBookV2 (2.0): same duplicate-id policy, pool-backed storage.
// ---------------------------------------------------------------------

TEST(OrderBookV2Test, DuplicateOrderIdRejected) {
    OrderBookV2 book(/*orderPoolCapacity=*/10);
    EXPECT_TRUE(book.addOrder(makeOrder(1, 100)));
    EXPECT_FALSE(book.addOrder(makeOrder(1, 200)));
    EXPECT_TRUE(book.hasOrder(1));
}

// ---------------------------------------------------------------------
// OrderBookV3 (3.0): bounded price range (ADR-0020) — construction and
// per-order validation both need to reject out-of-range input loudly
// rather than corrupting the flat array.
// ---------------------------------------------------------------------

TEST(OrderBookV3Test, ConstructorRejectsInvertedRange) {
    EXPECT_THROW(OrderBookV3(200, 100, 1, 10), std::invalid_argument);
}

TEST(OrderBookV3Test, ConstructorRejectsZeroTickSize) {
    EXPECT_THROW(OrderBookV3(100, 200, 0, 10), std::invalid_argument);
}

TEST(OrderBookV3Test, AddOrderRejectsPriceBelowMinimum) {
    OrderBookV3 book(100, 200, 1, 10);
    EXPECT_THROW((void)book.addOrder(makeOrder(1, 99)), std::out_of_range);
}

TEST(OrderBookV3Test, AddOrderRejectsPriceAboveMaximum) {
    OrderBookV3 book(100, 200, 1, 10);
    EXPECT_THROW((void)book.addOrder(makeOrder(1, 201)), std::out_of_range);
}

TEST(OrderBookV3Test, AddOrderAcceptsPriceAtBothBoundaries) {
    OrderBookV3 book(100, 200, 1, 10);
    EXPECT_TRUE(book.addOrder(makeOrder(1, 100)));
    EXPECT_TRUE(book.addOrder(makeOrder(2, 200)));
}

TEST(OrderBookV3Test, DuplicateOrderIdRejected) {
    OrderBookV3 book(100, 200, 1, 10);
    EXPECT_TRUE(book.addOrder(makeOrder(1, 150)));
    EXPECT_FALSE(book.addOrder(makeOrder(1, 160)));
}

// A thin market getting fully swept — every resting order on one side
// consumed by a single incoming order — is a real scenario, not just a
// coverage target: nextOccupiedFrom/prevOccupiedFrom's "nothing left"
// return paths only ever run when a side goes completely empty, which none
// of the differential-test workloads happen to drive all the way to.
TEST(OrderBookV3Test, MarketSweepCanFullyDrainOneSideWithoutCrashing) {
    OrderBookV3 book(100, 200, 1, 10);
    EXPECT_TRUE(book.addOrder(makeOrder(1, 150, 10, Side::Sell)));
    EXPECT_TRUE(book.addOrder(makeOrder(2, 200, 10, Side::Sell))); // top of the configured range

    Order sweeper = makeOrder(3, 0, 1000, Side::Buy);
    std::vector<Trade> trades;
    book.matchAgainst(sweeper, std::nullopt, trades);

    EXPECT_EQ(trades.size(), 2u);
    EXPECT_FALSE(book.hasOrder(1));
    EXPECT_FALSE(book.hasOrder(2));

    // The ask side is now fully empty — inserting again must still work
    // cleanly, proving the occupancy/best-tick bookkeeping didn't get left
    // in a stale state by the drain.
    EXPECT_TRUE(book.addOrder(makeOrder(4, 175, 5, Side::Sell)));
}

// ---------------------------------------------------------------------
// OrderBookV4 (4.0): same bounded price range as 3.0, plus a bounded
// OrderId range for the flat cancellation index (ADR-0022).
// ---------------------------------------------------------------------

TEST(OrderBookV4Test, ConstructorRejectsInvertedRange) {
    EXPECT_THROW(OrderBookV4(200, 100, 1, 10, 10), std::invalid_argument);
}

TEST(OrderBookV4Test, AddOrderRejectsPriceOutsideRange) {
    OrderBookV4 book(100, 200, 1, 10, 10);
    EXPECT_THROW((void)book.addOrder(makeOrder(1, 99)), std::out_of_range);
    EXPECT_THROW((void)book.addOrder(makeOrder(1, 201)), std::out_of_range);
}

TEST(OrderBookV4Test, AddOrderRejectsOrderIdAtOrBeyondCapacity) {
    OrderBookV4 book(100, 200, 1, /*orderPoolCapacity=*/10, /*orderIdCapacity=*/5);
    EXPECT_THROW((void)book.addOrder(makeOrder(5, 150)), std::out_of_range); // id == capacity
    EXPECT_THROW((void)book.addOrder(makeOrder(100, 150)), std::out_of_range);
}

TEST(OrderBookV4Test, AddOrderAcceptsOrderIdAtTheLastValidIndex) {
    OrderBookV4 book(100, 200, 1, 10, /*orderIdCapacity=*/5);
    EXPECT_TRUE(book.addOrder(makeOrder(4, 150))); // valid indices are 0..4
}

TEST(OrderBookV4Test, HasOrderReturnsFalseRatherThanThrowingForOutOfRangeId) {
    OrderBookV4 book(100, 200, 1, 10, /*orderIdCapacity=*/5);
    EXPECT_FALSE(book.hasOrder(999)); // routine query, not a caller error — must not throw
}

TEST(OrderBookV4Test, CancelOrderReturnsFalseRatherThanThrowingForOutOfRangeId) {
    OrderBookV4 book(100, 200, 1, 10, /*orderIdCapacity=*/5);
    EXPECT_FALSE(book.cancelOrder(999));
}

TEST(OrderBookV4Test, DuplicateOrderIdRejected) {
    OrderBookV4 book(100, 200, 1, 10, 10);
    EXPECT_TRUE(book.addOrder(makeOrder(1, 150)));
    EXPECT_FALSE(book.addOrder(makeOrder(1, 160)));
}

TEST(OrderBookV4Test, MarketSweepCanFullyDrainOneSideWithoutCrashing) {
    OrderBookV4 book(100, 200, 1, 10, 10);
    EXPECT_TRUE(book.addOrder(makeOrder(1, 150, 10, Side::Sell)));
    EXPECT_TRUE(book.addOrder(makeOrder(2, 200, 10, Side::Sell)));

    Order sweeper = makeOrder(3, 0, 1000, Side::Buy);
    std::vector<Trade> trades;
    book.matchAgainst(sweeper, std::nullopt, trades);

    EXPECT_EQ(trades.size(), 2u);
    EXPECT_FALSE(book.hasOrder(1));
    EXPECT_FALSE(book.hasOrder(2));

    // The best-tick cache specifically needs to have noticed the drain —
    // this is the scenario noteMaybeBestCleared exists for, taken all the
    // way to "there is no best tick left at all."
    EXPECT_TRUE(book.addOrder(makeOrder(4, 175, 5, Side::Sell)));
}

TEST(OrderBookV4Test, CancelThenReinsertAtSameIdSucceeds) {
    // Exercises the "nodeIndex back to kInvalidNodeIndex on cancel" sentinel
    // reset directly — a duplicate-id false positive here would mean a
    // cancelled slot is still being treated as occupied.
    OrderBookV4 book(100, 200, 1, 10, 10);
    EXPECT_TRUE(book.addOrder(makeOrder(1, 150)));
    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_FALSE(book.hasOrder(1));
    EXPECT_TRUE(book.addOrder(makeOrder(1, 160)));
    EXPECT_TRUE(book.hasOrder(1));
}
