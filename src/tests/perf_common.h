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
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace mib::perf {

struct LatencyStats {
    std::size_t n{0};
    double minUs{0.0};
    double meanUs{0.0};
    double medianUs{0.0};
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
    return s;
}

inline void logStats(const std::string& label, const LatencyStats& s) {
    SPDLOG_INFO("{} | n={} min={:.3f}us median={:.3f}us mean={:.3f}us "
                "p95={:.3f}us p99={:.3f}us max={:.3f}us",
                label, s.n, s.minUs, s.medianUs, s.meanUs, s.p95Us, s.p99Us, s.maxUs);
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
