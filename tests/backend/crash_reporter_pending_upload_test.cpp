// Verifies the CrashReporter pending-dump upload and legacy-recovery paths.
//
// Acceptance criteria covered:
//   - Pending .dmp files are submitted via sentry_capture_minidump only when
//     Sentry is actually active, and renamed to .queued (not .uploaded).
//   - When Sentry is inactive (no DSN, init failure, or built without
//     Sentry), pending dumps remain untouched so a later launch can submit
//     them.
//   - Legacy .dmp.uploaded files are recovered to .dmp for re-submission.
//   - Bounded retention removes oldest .queued dumps beyond the limit, and
//     also bounds orphan .json sidecars while preserving sidecars of dumps
//     still on disk.
//
// Checks use an explicit failure counter (not assert) so the test still
// verifies behavior in Release/NDEBUG builds — every CI lane builds Release.

#include "backend/services/CrashReporter.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using CrashReporter = backend::services::CrashReporter;

static int g_failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": "     \
                      << #cond << "\n";                                     \
            ++g_failures;                                                   \
        }                                                                   \
    } while (0)

static void createFile(const fs::path& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    CHECK(f.is_open());
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

static CrashReporter::Config baseConfig(const fs::path& dir) {
    CrashReporter::Config cfg;
    cfg.crashDir = dir;
    cfg.databaseDir = dir / "sentry-db";
    cfg.uploadPendingOnStart = true;
    cfg.installSignalHandlers = false;
    cfg.installTerminateHandler = false;
    cfg.installQtMessageHandler = false;
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    // Syntactically valid DSN; whether sentry_init actually succeeds is
    // environment-dependent (e.g. crashpad_handler presence), so tests key
    // their expectations off the observed isSentryActive() instead.
    cfg.dsn = "https://fake@localhost/1";
#endif
    return cfg;
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

    CrashReporter::init(baseConfig(dir));
    const bool active = CrashReporter::isSentryActive();
    CrashReporter::shutdown();

    // Recovery always renames .dmp.uploaded → .dmp. What happens next
    // depends on whether Sentry actually initialized:
    //   active   → submitted and renamed to .dmp.queued
    //   inactive → the recovered .dmp stays for a later launch
    CHECK(!fileExists(dir / "20260101T120000-pid1234-sigsegv.dmp.uploaded"));
    CHECK(!fileExists(dir / "20260101T120000-pid1234-sigsegv.json.uploaded"));
    if (active) {
        CHECK(!fileExists(dir / "20260101T120000-pid1234-sigsegv.dmp"));
        CHECK(fileExists(dir / "20260101T120000-pid1234-sigsegv.dmp.queued"));
        CHECK(fileExists(dir / "20260101T120000-pid1234-sigsegv.json.queued"));
    } else {
        CHECK(fileExists(dir / "20260101T120000-pid1234-sigsegv.dmp"));
        CHECK(fileExists(dir / "20260101T120000-pid1234-sigsegv.json"));
    }

    // The orphan .json.uploaded stays (no matching .dmp.uploaded).
    CHECK(fileExists(dir / "20260202T010000-pid9999-seh.json.uploaded"));

    cleanDir(dir);
    std::cout << "done\n";
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

    CrashReporter::init(baseConfig(dir));
    const bool active = CrashReporter::isSentryActive();
    CrashReporter::shutdown();

    if (active) {
        // With live Sentry: .dmp → .dmp.queued (not .uploaded).
        CHECK(!fileExists(dir / "20260301T080000-pid100-sigsegv.dmp"));
        CHECK(fileExists(dir / "20260301T080000-pid100-sigsegv.dmp.queued"));
        CHECK(!fileExists(dir / "20260301T080000-pid100-sigsegv.json"));
        CHECK(fileExists(dir / "20260301T080000-pid100-sigsegv.json.queued"));

        CHECK(!fileExists(dir / "20260302T090000-pid200-sigabrt.dmp"));
        CHECK(fileExists(dir / "20260302T090000-pid200-sigabrt.dmp.queued"));
        // If the second dump's sidecar doesn't exist, no .json.queued for it.
        CHECK(!fileExists(dir / "20260302T090000-pid200-sigabrt.json.queued"));
    } else {
        // Without live Sentry: nothing is submitted, dumps and sidecars
        // remain untouched for a later launch.
        CHECK(fileExists(dir / "20260301T080000-pid100-sigsegv.dmp"));
        CHECK(fileExists(dir / "20260301T080000-pid100-sigsegv.json"));
        CHECK(fileExists(dir / "20260302T090000-pid200-sigabrt.dmp"));
    }

    // The old .uploaded suffix must never appear.
    CHECK(!fileExists(dir / "20260301T080000-pid100-sigsegv.dmp.uploaded"));
    CHECK(!fileExists(dir / "20260302T090000-pid200-sigabrt.dmp.uploaded"));

    cleanDir(dir);
    std::cout << "done\n";
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

    auto cfg = baseConfig(dir);
    cfg.maxRetainedDumps = 3;
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
    CHECK(remaining <= 3);

    cleanDir(dir);
    std::cout << "done\n";
}

// ── Test 4: orphan sidecar retention ──────────────────────────────
static void testOrphanSidecarRetention() {
    std::cout << "  test: orphan .json sidecar retention ... ";
    auto dir = makeTempDir("orphan_json");

    // 5 orphan sidecars (terminate/crashpad-style, no matching dump).
    for (int i = 0; i < 5; ++i) {
        std::string name = "2026040" + std::to_string(i + 1) +
                           "T120000-pid" + std::to_string(i) + "-terminate.json";
        createFile(dir / name, R"({"reason":"terminate"})");
    }
    // One pending dump with its sidecar — the sidecar must survive
    // retention even when Sentry is inactive and the dump stays pending.
    createFile(dir / "20260410T120000-pid9-sigsegv.dmp", "MDMP pending");
    createFile(dir / "20260410T120000-pid9-sigsegv.json",
               R"({"snapshot":"pending"})");

    auto cfg = baseConfig(dir);
    cfg.maxRetainedDumps = 3;
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    cfg.dsn.clear();  // force Sentry-inactive: the pending pair must survive
#endif
    CrashReporter::init(cfg);
    CrashReporter::shutdown();

    int orphansRemaining = 0;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        const std::string fn = entry.path().filename().string();
        if (fn.find("-terminate.json") != std::string::npos) {
            ++orphansRemaining;
        }
    }
    CHECK(orphansRemaining <= 3);

    // The pending dump and its sidecar are untouched.
    CHECK(fileExists(dir / "20260410T120000-pid9-sigsegv.dmp"));
    CHECK(fileExists(dir / "20260410T120000-pid9-sigsegv.json"));

    cleanDir(dir);
    std::cout << "done\n";
}

// ── Test 5: Sentry-inactive gating ────────────────────────────────
static void testSentryInactiveGating() {
    std::cout << "  test: no-DSN init leaves dumps untouched ... ";
    auto dir = makeTempDir("sentry_gate");

    createFile(dir / "20260601T120000-pid7-sigsegv.dmp", "MDMP gate test");

    // Before init: not active.
    CHECK(!CrashReporter::isSentryActive());

    auto cfg = baseConfig(dir);
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    cfg.dsn.clear();  // no DSN → local-only, sentryActive stays false
#endif
    CrashReporter::init(cfg);
    CHECK(CrashReporter::isInitialized());
    CHECK(!CrashReporter::isSentryActive());
    CrashReporter::shutdown();

    // With Sentry inactive the pending dump must not be renamed or removed.
    CHECK(fileExists(dir / "20260601T120000-pid7-sigsegv.dmp"));
    CHECK(!fileExists(dir / "20260601T120000-pid7-sigsegv.dmp.queued"));
    CHECK(!fileExists(dir / "20260601T120000-pid7-sigsegv.dmp.uploaded"));

    cleanDir(dir);
    std::cout << "done\n";
}

// ── Test 6: idempotent recovery ───────────────────────────────────
static void testIdempotentRecovery() {
    std::cout << "  test: repeated init is idempotent for recovery ... ";
    auto dir = makeTempDir("idempotent");

    createFile(dir / "20260401T120000-pid500-seh.dmp.uploaded",
               "MDMP idempotent test");

    CrashReporter::init(baseConfig(dir));
    const bool active = CrashReporter::isSentryActive();
    CrashReporter::shutdown();

    // Second init should not fail or double-process the dump.
    CrashReporter::init(baseConfig(dir));
    CrashReporter::shutdown();

    // The file should NOT be .uploaded anymore.
    CHECK(!fileExists(dir / "20260401T120000-pid500-seh.dmp.uploaded"));
    if (active) {
        CHECK(fileExists(dir / "20260401T120000-pid500-seh.dmp.queued"));
    } else {
        CHECK(fileExists(dir / "20260401T120000-pid500-seh.dmp"));
    }

    cleanDir(dir);
    std::cout << "done\n";
}

int main() {
    std::cout << "crash_reporter_pending_upload_test\n";

    testLegacyRecovery();
    testPendingDumpLifecycle();
    testBoundedRetention();
    testOrphanSidecarRetention();
    testSentryInactiveGating();
    testIdempotentRecovery();

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All tests passed.\n";
    return 0;
}
