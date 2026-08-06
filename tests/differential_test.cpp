// Phase 4 scaffolding: this harness runs the same action sequence through
// two (eventually four) MatchingEngine implementations and asserts they
// produce byte-identical trade ledgers. Correctness across implementations
// is what makes the Phase 4 comparative study trustworthy — a faster
// OrderBook that quietly behaves differently is worthless, and this is the
// test that would catch it, at the exact action that caused it.
//
// Today there is only one implementation (OrderBook, i.e. 1.0), so the only
// test below is a self-consistency check on the harness machinery itself
// (determinism, ledger comparison) rather than a real cross-implementation
// comparison. The moment OrderBookV2 (2.0) exists, add a second TEST here
// comparing runLedger<OrderBook> against runLedger<OrderBookV2> — that is
// the actual point of this file.

#include <gtest/gtest.h>
#include "engine/MatchingEngine.h"
#include "engine/Order.h"
#include "engine/Trade.h"
#include "structures/OrderBookV2.h"
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

// TODO(Phase 4, Step 3 / Step 4): once OrderBookV3 / OrderBookV4 exist, add
// the same two tests again against each, following this exact pattern.
