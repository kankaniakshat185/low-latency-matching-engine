#include "benchmark/WorkloadGenerator.h"
#include "engine/MatchingEngine.h"
#include "utils/Timer.h"
#include <iostream>
#include <iomanip>
#include <string>

using namespace engine;
using namespace engine::benchmark;
using namespace engine::utils;

struct BenchmarkConfig {
    std::string name;
    size_t numActions;
    size_t numWarmupActions;
};

struct BenchmarkResult {
    std::string name;
    size_t ordersProcessed;
    uint64_t tradesGenerated;
    double elapsedSeconds;
    double throughputMillions;
    LatencyMetrics latency;
};

BenchmarkResult runBenchmark(const BenchmarkConfig& config, const std::vector<BenchmarkAction>& actions) {
    MatchingEngine engine;
    std::vector<uint64_t> latencies;
    latencies.reserve(config.numActions);

    uint64_t totalMatches = 0;
    
    // Warm-up phase
    for (size_t i = 0; i < config.numWarmupActions && i < actions.size(); ++i) {
        if (actions[i].actionType == ActionType::Insert) {
            engine.processOrder(actions[i].order);
        } else {
            engine.cancelOrder(actions[i].order.id);
        }
    }

    Timer totalTimer;
    totalTimer.start();

    // Measurement phase
    for (size_t i = config.numWarmupActions; i < actions.size(); ++i) {
        Timer opTimer;
        opTimer.start();

        if (actions[i].actionType == ActionType::Insert) {
            auto trades = engine.processOrder(actions[i].order);
            totalMatches += trades.size();
        } else {
            engine.cancelOrder(actions[i].order.id);
        }

        latencies.push_back(opTimer.elapsedNanos());
    }

    double totalElapsedSec = totalTimer.elapsedSeconds();
    size_t measuredActions = actions.size() - config.numWarmupActions;
    double throughput = measuredActions / totalElapsedSec;

    return BenchmarkResult{
        config.name,
        measuredActions,
        totalMatches,
        totalElapsedSec,
        throughput / 1e6,
        calculateMetrics(latencies)
    };
}

void printResult(const BenchmarkResult& result) {
    std::cout << "--- Benchmark: " << result.name << " ---\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Measured Actions  : " << result.ordersProcessed << "\n";
    std::cout << "Trades Generated  : " << result.tradesGenerated << "\n";
    std::cout << "Elapsed Time      : " << result.elapsedSeconds << " sec\n";
    std::cout << "Throughput        : " << result.throughputMillions << " M actions/sec\n\n";

    std::cout << "Latency (nanoseconds):\n";
    std::cout << "  Average : " << result.latency.average << " ns\n";
    std::cout << "  P50     : " << result.latency.p50 << " ns\n";
    std::cout << "  P90     : " << result.latency.p90 << " ns\n";
    std::cout << "  P95     : " << result.latency.p95 << " ns\n";
    std::cout << "  P99     : " << result.latency.p99 << " ns\n";
    std::cout << "  P99.9   : " << result.latency.p999 << " ns\n";
    std::cout << "  Max     : " << result.latency.max << " ns\n";
    std::cout << "=========================================\n\n";
}

int main() {
    WorkloadGenerator generator;
    
    // Benchmark configuration
    size_t totalActions = 1'100'000;
    size_t warmupActions = 100'000;
    size_t measuredActions = totalActions - warmupActions;

    BenchmarkConfig randomConfig{"Random Prices", measuredActions, warmupActions};
    BenchmarkConfig cancelsConfig{"Heavy Cancels", measuredActions, warmupActions};
    BenchmarkConfig worstCaseConfig{"Worst Case (Same Price)", measuredActions, warmupActions};

    std::cout << "Generating workloads (" << totalActions << " actions each)...\n\n";
    
    auto randomWorkload = generator.generateRandom(totalActions);
    auto cancelsWorkload = generator.generateHeavyCancels(totalActions);
    auto worstCaseWorkload = generator.generateWorstCase(totalActions);

    auto randomResult = runBenchmark(randomConfig, randomWorkload);
    auto cancelsResult = runBenchmark(cancelsConfig, cancelsWorkload);
    auto worstCaseResult = runBenchmark(worstCaseConfig, worstCaseWorkload);

    printResult(randomResult);
    printResult(cancelsResult);
    printResult(worstCaseResult);

    return 0;
}
