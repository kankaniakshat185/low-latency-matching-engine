// Phase 4 comparative study: runs the same synthetic workloads through
// multiple OrderBook variants (1.0 baseline, 2.0 pool + intrusive list, ...)
// and prints results side by side. Deliberately a separate binary from
// engine_benchmark — engine_benchmark's existing output and behavior are
// what the README's published 1.0/1.0.1 numbers come from, and this
// comparison shouldn't risk changing that in any way.

#include "benchmark/WorkloadGenerator.h"
#include "engine/MatchingEngine.h"
#include "structures/OrderBookV2.h"
#include "utils/Timer.h"
#include <iomanip>
#include <iostream>
#include <string>

// os_signpost marks each variant/workload run as a named interval so
// Instruments (CPU Counters, via `xctrace`) can attribute hardware-counter
// data to the correct run — otherwise all six runs (1.0 x3, 2.0 x3) blur
// together in one trace with no way to tell which bottleneck data belongs
// to which variant. Apple-only; compiles out entirely elsewhere (e.g. the
// Linux CI runner) so this never affects portability.
#if defined(__APPLE__)
#include <os/log.h>
#include <os/signpost.h>
#define ENGINE_HAS_SIGNPOST 1
#else
#define ENGINE_HAS_SIGNPOST 0
#endif

using namespace engine;
using namespace engine::benchmark;
using namespace engine::utils;

#if ENGINE_HAS_SIGNPOST
namespace {
os_log_t gSignpostLog = os_log_create("com.matchingengine.benchmark", OS_LOG_CATEGORY_POINTS_OF_INTEREST);

// RAII wrapper: begin on construction, end on destruction, so a named
// interval always closes exactly once regardless of how the scope exits.
class SignpostRegion {
public:
    explicit SignpostRegion(const char* name) : id_(os_signpost_id_generate(gSignpostLog)) {
        os_signpost_interval_begin(gSignpostLog, id_, "Benchmark Run", "%{public}s", name);
    }
    ~SignpostRegion() { os_signpost_interval_end(gSignpostLog, id_, "Benchmark Run"); }

private:
    os_signpost_id_t id_;
};
} // namespace
#define BENCHMARK_SIGNPOST(name) SignpostRegion signpostRegion_##__LINE__(name)
#else
#define BENCHMARK_SIGNPOST(name) \
    do {                         \
    } while (0)
#endif

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

// Same three-pass warm-up/throughput/latency methodology as engine_benchmark
// (see ADR-0010), templated on the engine type so it can run identically
// over MatchingEngine (1.0) and MatchingEngineT<OrderBookV2> (2.0, ...).
// `ctorArgs` are copied (not forwarded/moved) into each of the two engine
// constructions below on purpose — both need their own independent copy.
template <typename EngineT, typename... CtorArgs>
BenchmarkResult runBenchmark(const BenchmarkConfig& config, const std::vector<BenchmarkAction>& actions,
                             CtorArgs... ctorArgs) {
    EngineT engine(ctorArgs...);
    uint64_t totalMatches = 0;

    for (size_t i = 0; i < config.numWarmupActions && i < actions.size(); ++i) {
        if (actions[i].actionType == ActionType::Insert)
            engine.processOrder(actions[i].order);
        else
            (void)engine.cancelOrder(actions[i].order.id);
    }

    Timer totalTimer;
    totalTimer.start();
    for (size_t i = config.numWarmupActions; i < actions.size(); ++i) {
        if (actions[i].actionType == ActionType::Insert) {
            auto trades = engine.processOrder(actions[i].order);
            totalMatches += trades.size();
        } else {
            (void)engine.cancelOrder(actions[i].order.id);
        }
    }
    double totalElapsedSec = totalTimer.elapsedSeconds();

    EngineT engineForLatency(ctorArgs...);
    std::vector<uint64_t> latencies;
    latencies.reserve(actions.size() - config.numWarmupActions);

    for (size_t i = 0; i < config.numWarmupActions && i < actions.size(); ++i) {
        if (actions[i].actionType == ActionType::Insert)
            engineForLatency.processOrder(actions[i].order);
        else
            (void)engineForLatency.cancelOrder(actions[i].order.id);
    }

    for (size_t i = config.numWarmupActions; i < actions.size(); ++i) {
        Timer opTimer;
        if (actions[i].actionType == ActionType::Insert)
            engineForLatency.processOrder(actions[i].order);
        else
            (void)engineForLatency.cancelOrder(actions[i].order.id);
        latencies.push_back(opTimer.elapsedNanos());
    }

    size_t measuredActions = actions.size() - config.numWarmupActions;
    double throughput = measuredActions / totalElapsedSec;

    return BenchmarkResult{config.name,     measuredActions,  totalMatches,
                           totalElapsedSec, throughput / 1e6, calculateMetrics(latencies)};
}

void printComparison(const BenchmarkResult& v1, const BenchmarkResult& v2) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "--- " << v1.name << " ---\n";
    std::cout << std::left << std::setw(20) << "" << std::right << std::setw(16) << "1.0 (baseline)" << std::setw(20)
              << "2.0 (pool+list)" << std::setw(12) << "delta\n";
    double throughputDeltaPct = (v2.throughputMillions / v1.throughputMillions - 1.0) * 100.0;
    std::cout << std::left << std::setw(20) << "Throughput (M/s)" << std::right << std::setw(16)
              << v1.throughputMillions << std::setw(20) << v2.throughputMillions << std::setw(11) << std::showpos
              << throughputDeltaPct << "%" << std::noshowpos << "\n";
    std::cout << std::left << std::setw(20) << "P50 latency (ns)" << std::right << std::setw(16) << v1.latency.p50
              << std::setw(20) << v2.latency.p50 << "\n";
    std::cout << std::left << std::setw(20) << "P99 latency (ns)" << std::right << std::setw(16) << v1.latency.p99
              << std::setw(20) << v2.latency.p99 << "\n";
    std::cout << std::left << std::setw(20) << "Max latency (ns)" << std::right << std::setw(16) << v1.latency.max
              << std::setw(20) << v2.latency.max << "\n";
    std::cout << std::left << std::setw(20) << "Trades generated" << std::right << std::setw(16) << v1.tradesGenerated
              << std::setw(20) << v2.tradesGenerated
              << (v1.tradesGenerated == v2.tradesGenerated ? "  (match)" : "  (MISMATCH!)") << "\n";
    std::cout << "=========================================\n\n";
}

int main() {
    WorkloadGenerator generator;

    size_t totalActions = 1'100'000;
    size_t warmupActions = 100'000;
    size_t measuredActions = totalActions - warmupActions;
    // Safe upper bound for 2.0's fixed-capacity pool (ADR-0017): the number
    // of orders resting at once can never exceed the number ever inserted.
    size_t poolCapacity = totalActions;

    BenchmarkConfig randomConfig{"Random Prices", measuredActions, warmupActions};
    BenchmarkConfig cancelsConfig{"Heavy Cancels", measuredActions, warmupActions};
    BenchmarkConfig worstCaseConfig{"Worst Case (Same Price)", measuredActions, warmupActions};

    std::cout << "Generating synthetic workloads (" << totalActions << " actions each)...\n\n";
    auto randomWorkload = generator.generateRandom(totalActions);
    auto cancelsWorkload = generator.generateHeavyCancels(totalActions);
    auto worstCaseWorkload = generator.generateWorstCase(totalActions);

    using EngineV2 = MatchingEngineT<structures::OrderBookV2>;

    BenchmarkResult randomV1, randomV2, cancelsV1, cancelsV2, worstV1, worstV2;
    {
        BENCHMARK_SIGNPOST("1.0-Random");
        randomV1 = runBenchmark<MatchingEngine>(randomConfig, randomWorkload);
    }
    {
        BENCHMARK_SIGNPOST("2.0-Random");
        randomV2 = runBenchmark<EngineV2>(randomConfig, randomWorkload, poolCapacity);
    }
    printComparison(randomV1, randomV2);

    {
        BENCHMARK_SIGNPOST("1.0-HeavyCancels");
        cancelsV1 = runBenchmark<MatchingEngine>(cancelsConfig, cancelsWorkload);
    }
    {
        BENCHMARK_SIGNPOST("2.0-HeavyCancels");
        cancelsV2 = runBenchmark<EngineV2>(cancelsConfig, cancelsWorkload, poolCapacity);
    }
    printComparison(cancelsV1, cancelsV2);

    {
        BENCHMARK_SIGNPOST("1.0-WorstCase");
        worstV1 = runBenchmark<MatchingEngine>(worstCaseConfig, worstCaseWorkload);
    }
    {
        BENCHMARK_SIGNPOST("2.0-WorstCase");
        worstV2 = runBenchmark<EngineV2>(worstCaseConfig, worstCaseWorkload, poolCapacity);
    }
    printComparison(worstV1, worstV2);

    return 0;
}
