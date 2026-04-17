// Tiny shared helpers for the perf test executables.
//
// Factored out of thread_perf_test.cpp so framestore_perf_test,
// processing_perf_test, hdf5_perf_test, and capture_processing_test
// don't each reinvent a LatencyStats struct and JSON writer.
//
// Header-only on purpose: the perf tests are small single-TU binaries
// and we don't want a new library target.

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif

#include <spdlog/spdlog.h>

namespace mib::perf {

// ---------------------------------------------------------------------------
// LatencyStats — extended with stddev and coefficient of variation (CV%).
// ---------------------------------------------------------------------------

struct LatencyStats {
    std::size_t n{0};
    double minUs{0.0};
    double meanUs{0.0};
    double medianUs{0.0};
    double stddevUs{0.0};
    double cvPct{0.0}; // coefficient of variation = (stddev / mean) * 100
    double p95Us{0.0};
    double p99Us{0.0};
    double maxUs{0.0};
};

inline LatencyStats summarise(std::vector<double> samplesUs) {
    LatencyStats s;
    if (samplesUs.empty()) return s;
    std::sort(samplesUs.begin(), samplesUs.end());
    s.n = samplesUs.size();
    s.minUs = samplesUs.front();
    s.maxUs = samplesUs.back();
    s.medianUs = samplesUs[s.n / 2];
    const std::size_t p95Idx = std::min(s.n - 1, static_cast<std::size_t>(s.n * 0.95));
    const std::size_t p99Idx = std::min(s.n - 1, static_cast<std::size_t>(s.n * 0.99));
    s.p95Us = samplesUs[p95Idx];
    s.p99Us = samplesUs[p99Idx];
    s.meanUs = std::accumulate(samplesUs.begin(), samplesUs.end(), 0.0)
               / static_cast<double>(s.n);
    double sumSqDiff = 0.0;
    for (double x : samplesUs) sumSqDiff += (x - s.meanUs) * (x - s.meanUs);
    s.stddevUs = std::sqrt(sumSqDiff / static_cast<double>(s.n));
    s.cvPct = (s.meanUs > 0.0) ? (s.stddevUs / s.meanUs * 100.0) : 0.0;
    return s;
}

inline void logStats(const std::string& label, const LatencyStats& s) {
    SPDLOG_INFO("{} | n={} mean={:.3f}±{:.3f}us (CV {:.1f}%) "
                "median={:.3f}us p99={:.3f}us max={:.3f}us",
                label, s.n, s.meanUs, s.stddevUs, s.cvPct,
                s.medianUs, s.p99Us, s.maxUs);
}

// ---------------------------------------------------------------------------
// Memory helpers — peak RSS for before/after tracking.
// ---------------------------------------------------------------------------

inline std::size_t peakRssBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return pmc.PeakWorkingSetSize;
    return 0;
#else
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmHWM:", 0) == 0) {
            std::size_t kb = 0;
            std::sscanf(line.c_str(), "VmHWM: %zu kB", &kb);
            return kb * 1024;
        }
    }
    return 0;
#endif
}

inline double peakRssMB() {
    return static_cast<double>(peakRssBytes()) / (1024.0 * 1024.0);
}

// ---------------------------------------------------------------------------
// Machine info — logged once per test for CI-runner attribution.
// ---------------------------------------------------------------------------

inline std::string machineInfoJson() {
    std::ostringstream o;
    o << "{\"hw_threads\":" << std::thread::hardware_concurrency()
      << ",\"peak_rss_mb\":" << std::fixed << std::setprecision(1) << peakRssMB()
      << "}";
    return o.str();
}

// ---------------------------------------------------------------------------
// Warmup helper — run a callable N times, discarding results.
// ---------------------------------------------------------------------------

template <typename Fn>
inline void warmUp(Fn&& fn, std::size_t n = 10) {
    for (std::size_t i = 0; i < n; ++i) fn();
}

inline std::size_t envSizeOr(const char* key, std::size_t fallback) {
    if (const char* v = std::getenv(key)) {
        try { return static_cast<std::size_t>(std::stoull(v)); } catch (...) {}
    }
    return fallback;
}

inline std::string statsJson(const LatencyStats& s) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(3)
      << "{"
      << "\"n\":" << s.n
      << ",\"min_us\":" << s.minUs
      << ",\"median_us\":" << s.medianUs
      << ",\"mean_us\":" << s.meanUs
      << ",\"stddev_us\":" << s.stddevUs
      << ",\"cv_pct\":" << s.cvPct
      << ",\"p95_us\":" << s.p95Us
      << ",\"p99_us\":" << s.p99Us
      << ",\"max_us\":" << s.maxUs
      << "}";
    return o.str();
}

// Minimal JSON object writer — takes a list of (key, already-serialised-value)
// pairs and emits a JSON object. Keeps the perf tests dependency-free (no
// nlohmann::json pull-in for a one-shot report).
class JsonReport {
public:
    JsonReport& add(const std::string& key, const std::string& serialisedValue) {
        entries_.emplace_back(key, serialisedValue);
        return *this;
    }
    JsonReport& addStats(const std::string& key, const LatencyStats& s) {
        return add(key, statsJson(s));
    }
    JsonReport& addNumber(const std::string& key, double v) {
        std::ostringstream o; o << std::fixed << std::setprecision(6) << v;
        return add(key, o.str());
    }
    JsonReport& addInt(const std::string& key, long long v) {
        return add(key, std::to_string(v));
    }
    JsonReport& addString(const std::string& key, const std::string& v) {
        std::ostringstream o; o << '"';
        for (char c : v) {
            if (c == '"' || c == '\\') o << '\\';
            o << c;
        }
        o << '"';
        return add(key, o.str());
    }

    bool writeTo(const std::string& path) const {
        std::ofstream out(path);
        if (!out) return false;
        out << "{\n";
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            out << "  \"" << entries_[i].first << "\": " << entries_[i].second;
            if (i + 1 < entries_.size()) out << ",";
            out << "\n";
        }
        out << "}\n";
        return true;
    }

private:
    std::vector<std::pair<std::string, std::string>> entries_;
};

inline std::string resolveJsonOutPath(const char* envKey, const std::string& fallback) {
    if (const char* v = std::getenv(envKey)) return v;
    return fallback;
}

} // namespace mib::perf
