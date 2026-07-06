#include <gtest/gtest.h>
#include "engine/MatchingEngine.h"
#include "engine/Order.h"
#include "engine/Types.h"
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

TEST_F(MatchingEngineTest, EmptyBookBehavior) {
    auto trades = engine.processOrder(Order(1, 100, 10, Side::Buy, OrderType::Limit));
    EXPECT_TRUE(trades.empty());
    
    trades = engine.processOrder(Order(2, 0, 10, Side::Sell, OrderType::Market));
    EXPECT_TRUE(trades.empty());
}

TEST_F(MatchingEngineTest, ExactMatch) {
    auto t1 = engine.processOrder(Order(1, 100, 10, Side::Sell, OrderType::Limit));
    EXPECT_TRUE(t1.empty());

    auto t2 = engine.processOrder(Order(2, 100, 10, Side::Buy, OrderType::Limit));
    ASSERT_EQ(t2.size(), 1);
    
    EXPECT_EQ(t2[0].makerOrderId, 1);
    EXPECT_EQ(t2[0].takerOrderId, 2);
    EXPECT_EQ(t2[0].executeQuantity, 10);
    EXPECT_EQ(t2[0].executePrice, 100);
}

TEST_F(MatchingEngineTest, PartialFillAggressive) {
    engine.processOrder(Order(1, 100, 5, Side::Sell, OrderType::Limit));
    
    // Aggressive buy order is larger than resting liquidity
    auto trades = engine.processOrder(Order(2, 100, 10, Side::Buy, OrderType::Limit));
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].executeQuantity, 5);
    EXPECT_EQ(trades[0].executePrice, 100);

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
    EXPECT_EQ(trades[0].executeQuantity, 5);
    
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
    EXPECT_EQ(trades[0].executeQuantity, 10);
    
    EXPECT_EQ(trades[1].makerOrderId, 2);
    EXPECT_EQ(trades[1].executeQuantity, 5);
}

TEST_F(MatchingEngineTest, BetterPricePriority) {
    engine.processOrder(Order(1, 101, 10, Side::Sell, OrderType::Limit)); // Worse price
    engine.processOrder(Order(2, 100, 10, Side::Sell, OrderType::Limit)); // Better price
    
    // Buy at 101 should match with 100 first
    auto trades = engine.processOrder(Order(3, 101, 10, Side::Buy, OrderType::Limit));
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].makerOrderId, 2);
    EXPECT_EQ(trades[0].executePrice, 100);
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

TEST_F(MatchingEngineTest, MarketOrderDiscardRemainder) {
    engine.processOrder(Order(1, 100, 5, Side::Sell, OrderType::Limit));
    
    // Market buy for 10
    auto trades = engine.processOrder(Order(2, 0, 10, Side::Buy, OrderType::Market));
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].executeQuantity, 5);
    
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
        Order(1, 100, 50, Side::Sell, OrderType::Limit),
        Order(2, 101, 50, Side::Sell, OrderType::Limit),
        Order(3, 99,  50, Side::Buy,  OrderType::Limit),
        Order(4, 98,  50, Side::Buy,  OrderType::Limit)
    };

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
    EXPECT_EQ(trades[0].executeQuantity, 50);
}
