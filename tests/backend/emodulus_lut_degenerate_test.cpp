// Fault-injection for EModulusLut file loading: a degenerate LUT whose area
// (or deform) column is constant used to build a grid with step == 0; a
// lookup at that value then computed 0.0/0.0 = NaN and cast it to size_t —
// UB that indexed grid_ far out of bounds. loadFromFile must reject such
// files, and lookup must stay in bounds regardless.

#include "backend/processing/EModulusLut.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

namespace
{
std::filesystem::path makeTempDir()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const auto path = std::filesystem::temp_directory_path() /
                          ("mib_lut_degenerate_" + std::to_string(dist(gen)));
        std::error_code ec;
        if (std::filesystem::create_directories(path, ec))
        {
            return path;
        }
    }
    std::exit(99);
}

bool writeLut(const std::filesystem::path& path, const std::string& body)
{
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << body;
    return true;
}
} // namespace

int main()
{
    const auto dir = makeTempDir();
    int failures = 0;
    auto expect = [&failures](bool cond, const char* what) {
        if (!cond)
        {
            std::cerr << "FAIL: " << what << "\n";
            ++failures;
        }
    };

    // Constant area column -> zero area range -> must be rejected.
    {
        const auto path = dir / "constant_area.txt";
        writeLut(path, "100.0\t0.01\t1.0\n100.0\t0.02\t2.0\n100.0\t0.03\t3.0\n");
        backend::EModulusLut lut;
        expect(!lut.loadFromFile(path.string()),
               "LUT with constant area column must be rejected");
    }

    // Constant deform column -> zero deform range -> must be rejected.
    {
        const auto path = dir / "constant_deform.txt";
        writeLut(path, "100.0\t0.01\t1.0\n200.0\t0.01\t2.0\n300.0\t0.01\t3.0\n");
        backend::EModulusLut lut;
        expect(!lut.loadFromFile(path.string()),
               "LUT with constant deform column must be rejected");
    }

    // Single-row file (both ranges zero) -> must be rejected.
    {
        const auto path = dir / "single_row.txt";
        writeLut(path, "100.0\t0.01\t1.0\n");
        backend::EModulusLut lut;
        expect(!lut.loadFromFile(path.string()),
               "single-row LUT must be rejected");
    }

    // A well-formed LUT still loads, and lookups (including at the exact
    // bounds, which exercise the index clamping) return finite values.
    {
        const auto path = dir / "valid.txt";
        writeLut(path,
                 "100.0\t0.01\t1.0\n"
                 "100.0\t0.05\t2.0\n"
                 "300.0\t0.01\t3.0\n"
                 "300.0\t0.05\t4.0\n");
        backend::EModulusLut lut;
        expect(lut.loadFromFile(path.string()), "well-formed LUT loads");
        expect(std::isfinite(lut.lookup(200.0, 0.03)), "interior lookup is finite");
        expect(std::isfinite(lut.lookup(100.0, 0.01)), "min-corner lookup is finite");
        expect(std::isfinite(lut.lookup(300.0, 0.05)), "max-corner lookup is finite");
        expect(std::isnan(lut.lookup(50.0, 0.03)), "out-of-range lookup returns NaN");
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);

    if (failures == 0)
    {
        std::cout << "EModulusLut degenerate-file test passed\n";
        return 0;
    }
    return 1;
}
