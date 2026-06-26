// Latency / distribution helpers for performance-budget tests.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace mib::test {

struct Stats {
    double min{0.0};
    double max{0.0};
    double mean{0.0};
    double p50{0.0};
    double p99{0.0};
    double stddev{0.0};
    std::size_t n{0};
};

inline double percentile(std::vector<double> v, double p)
{
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const double idx = (p / 100.0) * static_cast<double>(v.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(idx);
    const std::size_t hi = std::min(lo + 1, v.size() - 1);
    const double frac = idx - static_cast<double>(lo);
    return v[lo] * (1.0 - frac) + v[hi] * frac;
}

inline Stats summarize(const std::vector<double>& v)
{
    Stats s;
    if (v.empty()) {
        return s;
    }
    s.n = v.size();
    double sum = 0.0;
    s.min = v.front();
    s.max = v.front();
    for (double x : v) {
        sum += x;
        s.min = std::min(s.min, x);
        s.max = std::max(s.max, x);
    }
    s.mean = sum / static_cast<double>(v.size());
    double var = 0.0;
    for (double x : v) {
        var += (x - s.mean) * (x - s.mean);
    }
    s.stddev = std::sqrt(var / static_cast<double>(v.size()));
    s.p50 = percentile(v, 50.0);
    s.p99 = percentile(v, 99.0);
    return s;
}

} // namespace mib::test
