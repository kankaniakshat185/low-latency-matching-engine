#include "test_framework.h"
#include "engine/MatchingEngine.h"

using namespace engine;

void testSingleLimitOrder() {
    MatchingEngine engine;
    Order buyOrder(1, 100, 50, Side::Buy, OrderType::Limit);
    auto trades = engine.processOrder(buyOrder);
    
    ASSERT_EQ(trades.size(), 0);
    const auto& book = engine.getOrderBook();
    ASSERT_EQ(book.getBids().size(), 1);
    ASSERT_EQ(book.getAsks().size(), 0);
    ASSERT_EQ(book.getBids().begin()->second.getTotalQuantity(), 50);
}

void testExactMatch() {
    MatchingEngine engine;
    engine.processOrder(Order(1, 100, 50, Side::Buy, OrderType::Limit));
    
    auto trades = engine.processOrder(Order(2, 100, 50, Side::Sell, OrderType::Limit));
    
    ASSERT_EQ(trades.size(), 1);
    ASSERT_EQ(trades[0].makerOrderId, 1);
    ASSERT_EQ(trades[0].takerOrderId, 2);
    ASSERT_EQ(trades[0].price, 100);
    ASSERT_EQ(trades[0].quantity, 50);
    
    const auto& book = engine.getOrderBook();
    ASSERT_EQ(book.getBids().size(), 0);
    ASSERT_EQ(book.getAsks().size(), 0);
}

void testPartialMatch() {
    MatchingEngine engine;
    engine.processOrder(Order(1, 100, 100, Side::Buy, OrderType::Limit));
    
    auto trades = engine.processOrder(Order(2, 100, 40, Side::Sell, OrderType::Limit));
    
    ASSERT_EQ(trades.size(), 1);
    ASSERT_EQ(trades[0].quantity, 40);
    
    const auto& book = engine.getOrderBook();
    ASSERT_EQ(book.getBids().size(), 1);
    ASSERT_EQ(book.getBids().begin()->second.getTotalQuantity(), 60);
}

void testPriceTimePriority() {
    MatchingEngine engine;
    // Add two buy orders at the same price
    engine.processOrder(Order(1, 100, 50, Side::Buy, OrderType::Limit));
    engine.processOrder(Order(2, 100, 50, Side::Buy, OrderType::Limit));
    
    // Add a buy order at a higher price
    engine.processOrder(Order(3, 101, 50, Side::Buy, OrderType::Limit));
    
    // Sell 120 quantity
    auto trades = engine.processOrder(Order(4, 99, 120, Side::Sell, OrderType::Limit));
    
    ASSERT_EQ(trades.size(), 3);
    
    // First trade should match the highest price
    ASSERT_EQ(trades[0].makerOrderId, 3);
    ASSERT_EQ(trades[0].price, 101);
    ASSERT_EQ(trades[0].quantity, 50);
    
    // Second trade should match the first order at price 100
    ASSERT_EQ(trades[1].makerOrderId, 1);
    ASSERT_EQ(trades[1].price, 100);
    ASSERT_EQ(trades[1].quantity, 50);
    
    // Third trade should match the second order at price 100
    ASSERT_EQ(trades[2].makerOrderId, 2);
    ASSERT_EQ(trades[2].price, 100);
    ASSERT_EQ(trades[2].quantity, 20);
    
    const auto& book = engine.getOrderBook();
    ASSERT_EQ(book.getBids().size(), 1);
    ASSERT_EQ(book.getBids().begin()->second.getTotalQuantity(), 30);
}

void testCancelOrder() {
    MatchingEngine engine;
    engine.processOrder(Order(1, 100, 50, Side::Buy, OrderType::Limit));
    engine.processOrder(Order(2, 100, 50, Side::Buy, OrderType::Limit));
    
    bool canceled = engine.cancelOrder(1);
    ASSERT_TRUE(canceled);
    
    const auto& book = engine.getOrderBook();
    ASSERT_EQ(book.getBids().size(), 1);
    ASSERT_EQ(book.getBids().begin()->second.getTotalQuantity(), 50);
    
    auto trades = engine.processOrder(Order(3, 100, 50, Side::Sell, OrderType::Limit));
    ASSERT_EQ(trades.size(), 1);
    ASSERT_EQ(trades[0].makerOrderId, 2);
}

void testMarketOrder() {
    MatchingEngine engine;
    engine.processOrder(Order(1, 100, 50, Side::Sell, OrderType::Limit));
    engine.processOrder(Order(2, 105, 50, Side::Sell, OrderType::Limit));
    
    // Market buy of 70
    auto trades = engine.processOrder(Order(3, 0, 70, Side::Buy, OrderType::Market));
    
    ASSERT_EQ(trades.size(), 2);
    ASSERT_EQ(trades[0].makerOrderId, 1);
    ASSERT_EQ(trades[0].quantity, 50);
    
    ASSERT_EQ(trades[1].makerOrderId, 2);
    ASSERT_EQ(trades[1].quantity, 20);
    
    const auto& book = engine.getOrderBook();
    ASSERT_EQ(book.getAsks().size(), 1);
    ASSERT_EQ(book.getAsks().begin()->second.getTotalQuantity(), 30);
}

void runAllTests() {
    testSingleLimitOrder();
    testExactMatch();
    testPartialMatch();
    testPriceTimePriority();
    testCancelOrder();
    testMarketOrder();
}
