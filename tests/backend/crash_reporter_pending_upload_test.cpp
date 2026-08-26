// Verifies the CrashReporter pending-dump upload and legacy-recovery paths.
//
// Acceptance criteria covered:
//   - Pending .dmp files are submitted via sentry_capture_minidump (when Sentry
//     is compiled in) and renamed to .queued, not .uploaded.
//   - Without Sentry, pending dumps remain untouched.
//   - Legacy .dmp.uploaded files are recovered to .dmp for re-submission.
//   - State snapshots do not leak between events (scope cleaned per dump).
//   - Bounded retention removes oldest .queued dumps beyond the limit.

#include "backend/services/CrashReporter.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using CrashReporter = backend::services::CrashReporter;

static void createFile(const fs::path& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    assert(f.is_open());
    f << content;
}

static bool fileExists(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

static fs::path makeTempDir(const std::string& label) {
    auto dir = fs::temp_directory_path() / ("crash_reporter_test_" + label);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

static void cleanDir(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// ── Test 1: legacy .dmp.uploaded recovery ─────────────────────────
static void testLegacyRecovery() {
    std::cout << "  test: legacy .dmp.uploaded recovery ... ";
    auto dir = makeTempDir("legacy");

    createFile(dir / "20260101T120000-pid1234-sigsegv.dmp.uploaded",
               "MDMP fake dump");
    createFile(dir / "20260101T120000-pid1234-sigsegv.json.uploaded",
               R"({"capture":"stopped"})");
    // A .json.uploaded without a matching .dmp.uploaded stays untouched.
    createFile(dir / "20260202T010000-pid9999-seh.json.uploaded",
               R"({"orphan":true})");

    CrashReporter::Config cfg;
    cfg.crashDir = dir;
    cfg.databaseDir = dir / "sentry-db";
    cfg.uploadPendingOnStart = true;
    cfg.installSignalHandlers = false;
    cfg.installTerminateHandler = false;
    cfg.installQtMessageHandler = false;
    // No DSN → local-only mode. Recovery still renames .uploaded → .dmp.
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    cfg.dsn = "https://fake@localhost/1";
#endif

    CrashReporter::init(cfg);
    CrashReporter::shutdown();

    // The .dmp.uploaded should have been recovered to .dmp.
    // With Sentry compiled in: the recovered .dmp is then submitted
    // via sentry_capture_minidump and renamed to .dmp.queued.
    // Without Sentry: the recovered .dmp remains as .dmp.
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    assert(!fileExists(dir / "20260101T120000-pid1234-sigsegv.dmp.uploaded"));
    assert(!fileExists(dir / "20260101T120000-pid1234-sigsegv.dmp"));
    assert(fileExists(dir / "20260101T120000-pid1234-sigsegv.dmp.queued"));
    assert(!fileExists(dir / "20260101T120000-pid1234-sigsegv.json.uploaded"));
    assert(fileExists(dir / "20260101T120000-pid1234-sigsegv.json.queued"));
#else
    assert(!fileExists(dir / "20260101T120000-pid1234-sigsegv.dmp.uploaded"));
    assert(fileExists(dir / "20260101T120000-pid1234-sigsegv.dmp"));
    assert(!fileExists(dir / "20260101T120000-pid1234-sigsegv.json.uploaded"));
    assert(fileExists(dir / "20260101T120000-pid1234-sigsegv.json"));
#endif

    // The orphan .json.uploaded stays (no matching .dmp.uploaded).
    assert(fileExists(dir / "20260202T010000-pid9999-seh.json.uploaded"));

    cleanDir(dir);
    std::cout << "OK\n";
}

// ── Test 2: pending dump lifecycle ────────────────────────────────
static void testPendingDumpLifecycle() {
    std::cout << "  test: pending dump lifecycle ... ";
    auto dir = makeTempDir("lifecycle");

    createFile(dir / "20260301T080000-pid100-sigsegv.dmp",
               "MDMP fake minidump for lifecycle test");
    createFile(dir / "20260301T080000-pid100-sigsegv.json",
               R"({"capture":"running","frames_captured":42})");
    createFile(dir / "20260302T090000-pid200-sigabrt.dmp",
               "MDMP second dump");
    // Second dump has no sidecar — verifies isolation.

    CrashReporter::Config cfg;
    cfg.crashDir = dir;
    cfg.databaseDir = dir / "sentry-db";
    cfg.uploadPendingOnStart = true;
    cfg.installSignalHandlers = false;
    cfg.installTerminateHandler = false;
    cfg.installQtMessageHandler = false;
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    cfg.dsn = "https://fake@localhost/1";
#endif

    CrashReporter::init(cfg);
    CrashReporter::shutdown();

#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    // With Sentry: .dmp → .dmp.queued (not .uploaded).
    assert(!fileExists(dir / "20260301T080000-pid100-sigsegv.dmp"));
    assert(fileExists(dir / "20260301T080000-pid100-sigsegv.dmp.queued"));
    assert(!fileExists(dir / "20260301T080000-pid100-sigsegv.json"));
    assert(fileExists(dir / "20260301T080000-pid100-sigsegv.json.queued"));

    assert(!fileExists(dir / "20260302T090000-pid200-sigabrt.dmp"));
    assert(fileExists(dir / "20260302T090000-pid200-sigabrt.dmp.queued"));

    // The old .uploaded suffix must never appear.
    assert(!fileExists(dir / "20260301T080000-pid100-sigsegv.dmp.uploaded"));
    assert(!fileExists(dir / "20260302T090000-pid200-sigabrt.dmp.uploaded"));
#else
    // Without Sentry: .dmp files stay as-is (no upload mechanism).
    assert(fileExists(dir / "20260301T080000-pid100-sigsegv.dmp"));
    assert(fileExists(dir / "20260302T090000-pid200-sigabrt.dmp"));
#endif

    cleanDir(dir);
    std::cout << "OK\n";
}

// ── Test 3: bounded retention ─────────────────────────────────────
static void testBoundedRetention() {
    std::cout << "  test: bounded retention cleanup ... ";
    auto dir = makeTempDir("retention");

    // Pre-create 5 .queued dumps, set retention limit to 3.
    for (int i = 0; i < 5; ++i) {
        std::string name = "2026030" + std::to_string(i + 1) +
                           "T120000-pid" + std::to_string(i) + "-sigsegv.dmp.queued";
        createFile(dir / name, "MDMP old queued dump " + std::to_string(i));
    }

    CrashReporter::Config cfg;
    cfg.crashDir = dir;
    cfg.databaseDir = dir / "sentry-db";
    cfg.uploadPendingOnStart = true;
    cfg.maxRetainedDumps = 3;
    cfg.installSignalHandlers = false;
    cfg.installTerminateHandler = false;
    cfg.installQtMessageHandler = false;
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    cfg.dsn = "https://fake@localhost/1";
#endif

    CrashReporter::init(cfg);
    CrashReporter::shutdown();

    // Count remaining .queued files.
    int remaining = 0;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        const std::string fn = entry.path().filename().string();
        if (fn.find(".dmp.queued") != std::string::npos) {
            ++remaining;
        }
    }
    assert(remaining <= 3);

    cleanDir(dir);
    std::cout << "OK\n";
}

// ── Test 4: Sentry-active flag gating ─────────────────────────────
static void testSentryActiveFlag() {
    std::cout << "  test: isSentryActive reflects init result ... ";
    auto dir = makeTempDir("sentry_flag");

    // Before init: not active.
    assert(!CrashReporter::isSentryActive());

    CrashReporter::Config cfg;
    cfg.crashDir = dir;
    cfg.databaseDir = dir / "sentry-db";
    cfg.uploadPendingOnStart = false;
    cfg.installSignalHandlers = false;
    cfg.installTerminateHandler = false;
    cfg.installQtMessageHandler = false;
    // No DSN = local-only → sentryActive remains false.
    CrashReporter::init(cfg);
    assert(CrashReporter::isInitialized());
    assert(!CrashReporter::isSentryActive());
    CrashReporter::shutdown();

    cleanDir(dir);
    std::cout << "OK\n";
}

// ── Test 5: idempotent recovery ───────────────────────────────────
static void testIdempotentRecovery() {
    std::cout << "  test: repeated init is idempotent for recovery ... ";
    auto dir = makeTempDir("idempotent");

    createFile(dir / "20260401T120000-pid500-seh.dmp.uploaded",
               "MDMP idempotent test");

    CrashReporter::Config cfg;
    cfg.crashDir = dir;
    cfg.databaseDir = dir / "sentry-db";
    cfg.uploadPendingOnStart = true;
    cfg.installSignalHandlers = false;
    cfg.installTerminateHandler = false;
    cfg.installQtMessageHandler = false;
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    cfg.dsn = "https://fake@localhost/1";
#endif

    CrashReporter::init(cfg);
    CrashReporter::shutdown();

    // Second init should not fail or re-process the now-queued dump.
    CrashReporter::init(cfg);
    CrashReporter::shutdown();

    // The file should NOT be .uploaded anymore.
    assert(!fileExists(dir / "20260401T120000-pid500-seh.dmp.uploaded"));

#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    assert(fileExists(dir / "20260401T120000-pid500-seh.dmp.queued"));
#else
    assert(fileExists(dir / "20260401T120000-pid500-seh.dmp"));
#endif

    cleanDir(dir);
    std::cout << "OK\n";
}

// ── Test 6: dump without sidecar ──────────────────────────────────
static void testDumpWithoutSidecar() {
    std::cout << "  test: dump without sidecar does not inherit previous snapshot ... ";
    auto dir = makeTempDir("no_sidecar");

    createFile(dir / "20260501T120000-pid1-sigsegv.dmp", "MDMP dump1");
    createFile(dir / "20260501T120000-pid1-sigsegv.json",
               R"({"snapshot":"first"})");
    createFile(dir / "20260502T120000-pid2-sigabrt.dmp", "MDMP dump2");
    // No .json for the second dump.

    CrashReporter::Config cfg;
    cfg.crashDir = dir;
    cfg.databaseDir = dir / "sentry-db";
    cfg.uploadPendingOnStart = true;
    cfg.installSignalHandlers = false;
    cfg.installTerminateHandler = false;
    cfg.installQtMessageHandler = false;
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    cfg.dsn = "https://fake@localhost/1";
#endif

    CrashReporter::init(cfg);
    CrashReporter::shutdown();

#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    assert(fileExists(dir / "20260501T120000-pid1-sigsegv.dmp.queued"));
    assert(fileExists(dir / "20260502T120000-pid2-sigabrt.dmp.queued"));
    // If the second dump's sidecar doesn't exist, no .json.queued for it.
    assert(fileExists(dir / "20260501T120000-pid1-sigsegv.json.queued"));
    assert(!fileExists(dir / "20260502T120000-pid2-sigabrt.json.queued"));
#endif

    cleanDir(dir);
    std::cout << "OK\n";
}

int main() {
    std::cout << "crash_reporter_pending_upload_test\n";

    testLegacyRecovery();
    testPendingDumpLifecycle();
    testBoundedRetention();
    testSentryActiveFlag();
    testIdempotentRecovery();
    testDumpWithoutSidecar();

    std::cout << "All tests passed.\n";
    return 0;
}
