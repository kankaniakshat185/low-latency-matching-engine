#pragma once

#include <chrono>
#include <vector>
#include <numeric>
#include <algorithm>

namespace engine {
namespace utils {

class Timer {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    Timer() {
        start();
    }

    void start() {
        startTime_ = Clock::now();
    }

    // Returns elapsed time in nanoseconds
    uint64_t elapsedNanos() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - startTime_).count();
    }

    // Returns elapsed time in microseconds
    uint64_t elapsedMicros() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - startTime_).count();
    }

    // Returns elapsed time in seconds as double
    double elapsedSeconds() const {
        return std::chrono::duration<double>(Clock::now() - startTime_).count();
    }

private:
    TimePoint startTime_;
};

struct LatencyMetrics {
    uint64_t count;
    uint64_t min;
    uint64_t max;
    uint64_t average;
    uint64_t p50;
    uint64_t p90;
    uint64_t p95;
    uint64_t p99;
    uint64_t p999;
};

inline LatencyMetrics calculateMetrics(std::vector<uint64_t>& latencies) {
    if (latencies.empty()) {
        return {0, 0, 0, 0, 0, 0, 0, 0, 0};
    }

    std::sort(latencies.begin(), latencies.end());

    uint64_t count = latencies.size();
    uint64_t min = latencies.front();
    uint64_t max = latencies.back();
    
    uint64_t sum = std::accumulate(latencies.begin(), latencies.end(), 0ULL);
    uint64_t average = sum / count;

    auto percentile = [&](double p) -> uint64_t {
        size_t idx = static_cast<size_t>(p * static_cast<double>(count));
        if (idx >= count) idx = count - 1;
        return latencies[idx];
    };

    uint64_t p50  = percentile(0.50);
    uint64_t p90  = percentile(0.90);
    uint64_t p95  = percentile(0.95);
    uint64_t p99  = percentile(0.99);
    uint64_t p999 = percentile(0.999);

    return {count, min, max, average, p50, p90, p95, p99, p999};
}

} // namespace utils
} // namespace engine
