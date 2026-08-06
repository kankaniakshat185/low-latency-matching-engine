// Phase 4 scaffolding: this harness runs the same action sequence through
// four independent MatchingEngine implementations (1.0's OrderBook, 2.0's
// OrderBookV2, 3.0's OrderBookV3, 4.0's OrderBookV4) and asserts they
// produce byte-identical trade ledgers. Correctness across implementations
// is what makes the Phase 4 comparative study trustworthy — a faster
// OrderBook that quietly behaves differently is worthless, and this is the
// test that would catch it, at the exact action that caused it.
//
// HarnessIsDeterministic below is a sanity check on the harness machinery
// itself (determinism, ledger comparison); every VxMatchesBaseline* test
// after it is a real cross-implementation comparison against 1.0.

#include <gtest/gtest.h>
#include "engine/MatchingEngine.h"
#include "engine/Order.h"
#include "engine/Trade.h"
#include "structures/OrderBookV2.h"
#include "structures/OrderBookV3.h"
#include "structures/OrderBookV4.h"
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

using namespace engine;

namespace {

struct Action {
    enum class Kind { Insert, Cancel };
    Kind kind;
    Order order; // Fully populated for Insert; only order.id is meaningful for Cancel.
};

// Deterministic (seeded) random insert/cancel workload, biased toward more
// inserts than cancels so the book actually builds up depth to match
// against. Reused as-is once a second implementation exists — using the
// exact same generated action sequence for every variant is what makes the
// comparison apples-to-apples.
std::vector<Action> generateRandomActions(uint64_t seed, size_t count) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<Price> priceDist(9000, 11000);
    std::uniform_int_distribution<Quantity> qtyDist(1, 1000);
    std::uniform_int_distribution<int> sideDist(0, 1);
    std::uniform_int_distribution<int> cancelDist(0, 9); // ~20% cancel once orders exist

    std::vector<Action> actions;
    actions.reserve(count);
    std::vector<OrderId> liveIds;
    OrderId nextId = 1;

    for (size_t i = 0; i < count; ++i) {
        bool doCancel = !liveIds.empty() && cancelDist(rng) < 2;
        if (doCancel) {
            size_t idx = static_cast<size_t>(rng()) % liveIds.size();
            OrderId id = liveIds[idx];
            liveIds[idx] = liveIds.back();
            liveIds.pop_back();
            actions.push_back({Action::Kind::Cancel, Order(id, 0, 0, Side::Buy, OrderType::Limit)});
        } else {
            OrderId id = nextId++;
            Side side = sideDist(rng) == 0 ? Side::Buy : Side::Sell;
            actions.push_back({Action::Kind::Insert, Order(id, priceDist(rng), qtyDist(rng), side, OrderType::Limit)});
            liveIds.push_back(id);
        }
    }
    return actions;
}

// Runs `actions` through a fresh MatchingEngineT<BookT> and returns the full
// trade ledger, in the order trades were generated. Extra arguments forward
// to BookT's constructor — needed for variants like OrderBookV2 that take
// an explicit pool capacity (ADR-0017).
template <typename BookT, typename... BookCtorArgs>
std::vector<Trade> runLedger(const std::vector<Action>& actions, BookCtorArgs&&... bookCtorArgs) {
    MatchingEngineT<BookT> engine(std::forward<BookCtorArgs>(bookCtorArgs)...);
    std::vector<Trade> ledger;
    for (const auto& action : actions) {
        if (action.kind == Action::Kind::Insert) {
            auto trades = engine.processOrder(action.order);
            ledger.insert(ledger.end(), trades.begin(), trades.end());
        } else {
            (void)engine.cancelOrder(action.order.id);
        }
    }
    return ledger;
}

bool tradesEqual(const Trade& a, const Trade& b) {
    return a.makerOrderId == b.makerOrderId && a.takerOrderId == b.takerOrderId && a.price == b.price &&
           a.quantity == b.quantity;
}

// Shared assertion body: two ledgers must match trade-for-trade, in order.
// Compares length first so a divergence in count doesn't cascade into a
// wall of misleading per-trade failures.
void assertLedgersMatch(const std::vector<Trade>& expected, const std::vector<Trade>& actual) {
    ASSERT_EQ(expected.size(), actual.size()) << "ledgers diverge in trade count";
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_TRUE(tradesEqual(expected[i], actual[i]))
            << "divergence at trade index " << i << ": maker=" << expected[i].makerOrderId << " vs "
            << actual[i].makerOrderId << ", taker=" << expected[i].takerOrderId << " vs " << actual[i].takerOrderId
            << ", price=" << expected[i].price << " vs " << actual[i].price << ", qty=" << expected[i].quantity
            << " vs " << actual[i].quantity;
    }
}

} // namespace

TEST(DifferentialTest, HarnessIsDeterministic) {
    // Same seed, same workload, run twice through the same implementation:
    // this only proves the harness and OrderBook itself are both
    // deterministic. It is a sanity check on the machinery, not yet a real
    // cross-implementation comparison — that starts the moment a second
    // OrderBook variant exists.
    auto actions = generateRandomActions(/*seed=*/42, 5000);
    auto ledgerA = runLedger<OrderBook>(actions);
    auto ledgerB = runLedger<OrderBook>(actions);
    assertLedgersMatch(ledgerA, ledgerB);
    // A workload with real depth should actually produce trades — an empty
    // ledger here would mean the workload generator is broken, not that
    // the comparison trivially "passed".
    EXPECT_GT(ledgerA.size(), 0u);
}

TEST(DifferentialTest, V2MatchesBaseline) {
    // 200,000 actions, ~20% cancels — large enough to exercise deep books,
    // heavy churn, and repeated price-level creation/emptying on both
    // implementations. Pool capacity sized to the action count itself: a
    // safe (if generous) upper bound, since the number of orders resting at
    // once can never exceed the number ever inserted.
    constexpr size_t kActionCount = 200'000;
    auto actions = generateRandomActions(/*seed=*/1234, kActionCount);

    auto expected = runLedger<OrderBook>(actions);
    auto actual = runLedger<structures::OrderBookV2>(actions, kActionCount);

    assertLedgersMatch(expected, actual);
    EXPECT_GT(expected.size(), 0u);
}

TEST(DifferentialTest, V2MatchesBaselineOnWorstCaseSamePrice) {
    // A second, adversarial workload: every order at the same price, which
    // is exactly the scenario 2.0/3.0 are meant to help with. Correctness
    // matters most precisely where the implementations diverge the most
    // internally (one long std::list vs. one long intrusive list at a
    // single price level).
    constexpr size_t kActionCount = 50'000;
    std::vector<Action> actions;
    actions.reserve(kActionCount);
    std::mt19937_64 rng(5678);
    std::uniform_int_distribution<Quantity> qtyDist(1, 1000);
    for (size_t i = 0; i < kActionCount; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        actions.push_back(
            {Action::Kind::Insert, Order(static_cast<OrderId>(i + 1), 10000, qtyDist(rng), side, OrderType::Limit)});
    }

    auto expected = runLedger<OrderBook>(actions);
    auto actual = runLedger<structures::OrderBookV2>(actions, kActionCount);

    assertLedgersMatch(expected, actual);
    EXPECT_GT(expected.size(), 0u);
}

// OrderBookV3's price range/tick size must cover every price the workload
// generators can produce (they all draw from [9000, 11000] — see
// WorkloadGenerator.h and generateRandomActions above); ADR-0020 covers why
// this is a deliberate bound, not a limitation being papered over.
constexpr Price kV3MinPrice = 9000;
constexpr Price kV3MaxPrice = 11000;
constexpr Price kV3TickSize = 1;

TEST(DifferentialTest, V3MatchesBaseline) {
    constexpr size_t kActionCount = 200'000;
    auto actions = generateRandomActions(/*seed=*/1234, kActionCount);

    auto expected = runLedger<OrderBook>(actions);
    auto actual = runLedger<structures::OrderBookV3>(actions, kV3MinPrice, kV3MaxPrice, kV3TickSize, kActionCount);

    assertLedgersMatch(expected, actual);
    EXPECT_GT(expected.size(), 0u);
}

TEST(DifferentialTest, V3MatchesBaselineOnWorstCaseSamePrice) {
    constexpr size_t kActionCount = 50'000;
    std::vector<Action> actions;
    actions.reserve(kActionCount);
    std::mt19937_64 rng(5678);
    std::uniform_int_distribution<Quantity> qtyDist(1, 1000);
    for (size_t i = 0; i < kActionCount; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        actions.push_back(
            {Action::Kind::Insert, Order(static_cast<OrderId>(i + 1), 10000, qtyDist(rng), side, OrderType::Limit)});
    }

    auto expected = runLedger<OrderBook>(actions);
    auto actual = runLedger<structures::OrderBookV3>(actions, kV3MinPrice, kV3MaxPrice, kV3TickSize, kActionCount);

    assertLedgersMatch(expected, actual);
    EXPECT_GT(expected.size(), 0u);
}

// OrderBookV4 additionally needs an orderIdCapacity — both workload
// generators above hand out ids sequentially starting at 1, so
// kActionCount + 1 is a tight, safe upper bound (see the class comment on
// the flat OrderId-indexed cancellation index).
TEST(DifferentialTest, V4MatchesBaseline) {
    constexpr size_t kActionCount = 200'000;
    auto actions = generateRandomActions(/*seed=*/1234, kActionCount);

    auto expected = runLedger<OrderBook>(actions);
    auto actual = runLedger<structures::OrderBookV4>(actions, kV3MinPrice, kV3MaxPrice, kV3TickSize, kActionCount,
                                                      kActionCount + 1);

    assertLedgersMatch(expected, actual);
    EXPECT_GT(expected.size(), 0u);
}

TEST(DifferentialTest, V4MatchesBaselineOnWorstCaseSamePrice) {
    // This is the exact workload 4.0's cached best-tick exists for: every
    // order at the same price, so the best-price tick never moves once the
    // first order lands. If the cache logic has an off-by-one anywhere,
    // this is where it would produce a wrong trade, not just a slow one.
    constexpr size_t kActionCount = 50'000;
    std::vector<Action> actions;
    actions.reserve(kActionCount);
    std::mt19937_64 rng(5678);
    std::uniform_int_distribution<Quantity> qtyDist(1, 1000);
    for (size_t i = 0; i < kActionCount; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        actions.push_back(
            {Action::Kind::Insert, Order(static_cast<OrderId>(i + 1), 10000, qtyDist(rng), side, OrderType::Limit)});
    }

    auto expected = runLedger<OrderBook>(actions);
    auto actual = runLedger<structures::OrderBookV4>(actions, kV3MinPrice, kV3MaxPrice, kV3TickSize, kActionCount,
                                                      kActionCount + 1);

    assertLedgersMatch(expected, actual);
    EXPECT_GT(expected.size(), 0u);
}

// Phase 4, Step 5 — "bring it together": every test above checks one
// variant against 1.0 in isolation. This one runs a single shared workload
// through all four at once and cross-checks every pair against the
// baseline in one place — the closing correctness statement for the whole
// comparative study, not a new kind of check (a variant matching 1.0 and
// 1.0 matching itself already implies all four agree pairwise; this test
// exists to say so explicitly and keep saying so if that ever stops being
// true).
TEST(DifferentialTest, AllFourVersionsMatchOnASharedWorkload) {
    constexpr size_t kActionCount = 300'000;
    auto actions = generateRandomActions(/*seed=*/999, kActionCount);

    auto v1 = runLedger<OrderBook>(actions);
    auto v2 = runLedger<structures::OrderBookV2>(actions, kActionCount);
    auto v3 = runLedger<structures::OrderBookV3>(actions, kV3MinPrice, kV3MaxPrice, kV3TickSize, kActionCount);
    auto v4 = runLedger<structures::OrderBookV4>(actions, kV3MinPrice, kV3MaxPrice, kV3TickSize, kActionCount,
                                                  kActionCount + 1);

    assertLedgersMatch(v1, v2);
    assertLedgersMatch(v1, v3);
    assertLedgersMatch(v1, v4);
    EXPECT_GT(v1.size(), 0u);
}
