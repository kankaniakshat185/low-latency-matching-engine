#pragma once

#include "engine/Types.h"
#include "engine/Order.h"
#include <vector>
#include <random>

namespace engine {
namespace benchmark {

enum class ActionType {
    Insert,
    Cancel
};

struct BenchmarkAction {
    ActionType actionType;
    Order order; // Used if Insert, only OrderId used if Cancel
};

class WorkloadGenerator {
public:
    WorkloadGenerator(uint64_t seed = 42) : rng_(seed), orderIdCounter_(1) {}

    std::vector<BenchmarkAction> generateRandom(size_t numActions) {
        std::vector<BenchmarkAction> actions;
        actions.reserve(numActions);
        
        std::uniform_int_distribution<Price> priceDist(9000, 11000);
        std::uniform_int_distribution<Quantity> qtyDist(10, 1000);
        std::uniform_int_distribution<int> sideDist(0, 1);

        for (size_t i = 0; i < numActions; ++i) {
            Side side = sideDist(rng_) == 0 ? Side::Buy : Side::Sell;
            actions.push_back({
                ActionType::Insert, 
                Order(orderIdCounter_++, priceDist(rng_), qtyDist(rng_), side, OrderType::Limit)
            });
        }
        return actions;
    }

    std::vector<BenchmarkAction> generateWorstCase(size_t numActions) {
        // Worst case: everyone submits to the exact same price, causing a massive linked list
        std::vector<BenchmarkAction> actions;
        actions.reserve(numActions);
        
        std::uniform_int_distribution<Quantity> qtyDist(10, 1000);
        Price fixedPrice = 10000;

        for (size_t i = 0; i < numActions; ++i) {
            // Mix buys and sells at the same price to trigger massive matching
            Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
            actions.push_back({
                ActionType::Insert, 
                Order(orderIdCounter_++, fixedPrice, qtyDist(rng_), side, OrderType::Limit)
            });
        }
        return actions;
    }

    std::vector<BenchmarkAction> generateHeavyCancels(size_t numActions) {
        std::vector<BenchmarkAction> actions;
        actions.reserve(numActions);
        
        std::uniform_int_distribution<Price> priceDist(9000, 11000);
        std::uniform_int_distribution<Quantity> qtyDist(10, 1000);
        std::uniform_int_distribution<int> sideDist(0, 1);
        std::uniform_real_distribution<double> actionDist(0.0, 1.0);

        std::vector<OrderId> activeOrders;

        for (size_t i = 0; i < numActions; ++i) {
            // 70% insert, 30% cancel
            if (actionDist(rng_) < 0.7 || activeOrders.empty()) {
                OrderId id = orderIdCounter_++;
                Side side = sideDist(rng_) == 0 ? Side::Buy : Side::Sell;
                actions.push_back({
                    ActionType::Insert, 
                    Order(id, priceDist(rng_), qtyDist(rng_), side, OrderType::Limit)
                });
                activeOrders.push_back(id);
            } else {
                std::uniform_int_distribution<size_t> idxDist(0, activeOrders.size() - 1);
                size_t cancelIdx = idxDist(rng_);
                OrderId idToCancel = activeOrders[cancelIdx];
                
                // Swap with last and pop to remove efficiently
                activeOrders[cancelIdx] = activeOrders.back();
                activeOrders.pop_back();

                // Order data except ID doesn't matter for cancel
                actions.push_back({
                    ActionType::Cancel, 
                    Order(idToCancel, 0, 0, Side::Buy, OrderType::Limit) 
                });
            }
        }
        return actions;
    }

private:
    std::mt19937_64 rng_;
    OrderId orderIdCounter_;
};

} // namespace benchmark
} // namespace engine
