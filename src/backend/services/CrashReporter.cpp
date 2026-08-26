#include "backend/services/CrashReporter.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include <QtGlobal>
#include <QString>

#ifdef _WIN32
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <dbghelp.h>
#  include <process.h>
#  pragma comment(lib, "dbghelp.lib")
#else
#  include <unistd.h>
#endif

#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
#  include <sentry.h>
#endif

namespace backend::services {

namespace {

// Process-global state for the crash handler. All fields are read from the
// signal / SEH context, so they must use atomics / fixed buffers.
struct CrashGlobals {
    std::atomic<bool> initialized{false};
    std::atomic<bool> handlingCrash{false};
    CrashReporter::Config config{};
    CrashReporter::StateSnapshotFn stateSnapshot;
    std::mutex stateSnapshotMutex;
};

CrashGlobals& globals() {
    static CrashGlobals g;
    return g;
}

std::string isoTimestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%S", &tm);
    return std::string(buf);
}

std::filesystem::path makeCrashFilenameBase(const std::filesystem::path& dir,
                                            const std::string& reason) {
#ifdef _WIN32
    const unsigned pid = ::GetCurrentProcessId();
#else
    const unsigned pid = static_cast<unsigned>(::getpid());
#endif
    std::ostringstream os;
    os << isoTimestamp() << "-pid" << pid << "-" << reason;
    return dir / os.str();
}

double parseSampleRate(const char* value, double fallback) {
    if (!value || !*value) return fallback;
    char* end = nullptr;
    const double parsed = std::strtod(value, &end);
    if (end == value) return fallback;
    if (parsed < 0.0) return 0.0;
    if (parsed > 1.0) return 1.0;
    return parsed;
}

uint64_t epochMicrosNow() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

// Safe to call from a crash handler — only uses C-runtime file APIs.
void writeStateJsonSidecar(const std::filesystem::path& jsonPath) {
    auto& g = globals();
    std::string json;
    {
        // try_lock avoids deadlock if the crashing thread already holds
        // the snapshot mutex.
        std::unique_lock<std::mutex> lk(g.stateSnapshotMutex, std::defer_lock);
        if (lk.try_lock() && g.stateSnapshot) {
            json = g.stateSnapshot();
        } else if (g.stateSnapshot) {
            json = g.stateSnapshot();
        } else {
            json = R"({"note":"no state mirror registered"})";
        }
    }
    std::FILE* f = nullptr;
#ifdef _WIN32
    if (fopen_s(&f, jsonPath.string().c_str(), "wb") != 0) f = nullptr;
#else
    f = std::fopen(jsonPath.string().c_str(), "wb");
#endif
    if (!f) return;
    std::fwrite(json.data(), 1, json.size(), f);
    std::fflush(f);
    std::fclose(f);
}

#ifdef _WIN32
LONG writeMinidumpInternal(EXCEPTION_POINTERS* eptr,
                           const std::filesystem::path& dumpPath) {
    HANDLE hFile = ::CreateFileA(dumpPath.string().c_str(),
                                  GENERIC_READ | GENERIC_WRITE,
                                  0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    MINIDUMP_EXCEPTION_INFORMATION mdei{};
    if (eptr) {
        mdei.ThreadId = ::GetCurrentThreadId();
        mdei.ExceptionPointers = eptr;
        mdei.ClientPointers = FALSE;
    }

    const MINIDUMP_TYPE mdt = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithIndirectlyReferencedMemory |
        MiniDumpScanMemory |
        MiniDumpWithThreadInfo |
        MiniDumpWithUnloadedModules);

    BOOL ok = ::MiniDumpWriteDump(::GetCurrentProcess(),
                                   ::GetCurrentProcessId(),
                                   hFile,
                                   mdt,
                                   eptr ? &mdei : nullptr,
                                   nullptr,
                                   nullptr);
    ::CloseHandle(hFile);
    return ok ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI sehHandler(EXCEPTION_POINTERS* eptr) {
    auto& g = globals();
    bool expected = false;
    if (!g.handlingCrash.compare_exchange_strong(expected, true)) {
        // Already handling a crash; avoid recursion.
        return EXCEPTION_CONTINUE_SEARCH;
    }

    try {
        std::error_code ec;
        std::filesystem::create_directories(g.config.crashDir, ec);
        const auto base = makeCrashFilenameBase(g.config.crashDir, "seh");
        const auto dump = std::filesystem::path(base.string() + ".dmp");
        const auto json = std::filesystem::path(base.string() + ".json");
        writeMinidumpInternal(eptr, dump);
        writeStateJsonSidecar(json);
    } catch (...) {
        // Swallow — we are already crashing.
    }

#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    // sentry-native's own SEH handler is installed by sentry_init; let it run.
    return EXCEPTION_CONTINUE_SEARCH;
#else
    return EXCEPTION_CONTINUE_SEARCH; // let WER / debugger pick it up too
#endif
}
#endif // _WIN32

extern "C" void signalHandler(int sig) {
    auto& g = globals();
    bool expected = false;
    if (!g.handlingCrash.compare_exchange_strong(expected, true)) {
        std::_Exit(128 + sig);
    }
    const char* name = "signal";
    switch (sig) {
        case SIGSEGV: name = "sigsegv"; break;
        case SIGABRT: name = "sigabrt"; break;
        case SIGFPE:  name = "sigfpe"; break;
        case SIGILL:  name = "sigill"; break;
        default: break;
    }
    try {
        std::error_code ec;
        std::filesystem::create_directories(g.config.crashDir, ec);
        const auto base = makeCrashFilenameBase(g.config.crashDir, name);
#ifdef _WIN32
        const auto dump = std::filesystem::path(base.string() + ".dmp");
        writeMinidumpInternal(nullptr, dump);
#endif
        const auto json = std::filesystem::path(base.string() + ".json");
        writeStateJsonSidecar(json);
    } catch (...) {
        // Crashing already — never throw out of here.
    }
    // Re-raise with default disposition so debuggers / WER still get it.
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void terminateHandler() {
    auto& g = globals();
    bool expected = false;
    if (!g.handlingCrash.compare_exchange_strong(expected, true)) {
        std::abort();
    }
    try {
        std::error_code ec;
        std::filesystem::create_directories(g.config.crashDir, ec);
        const auto base = makeCrashFilenameBase(g.config.crashDir, "terminate");
        const auto json = std::filesystem::path(base.string() + ".json");
        writeStateJsonSidecar(json);
#ifdef _WIN32
        const auto dump = std::filesystem::path(base.string() + ".dmp");
        writeMinidumpInternal(nullptr, dump);
#endif
    } catch (...) {
    }
    std::abort();
}

void qtMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    const std::string m = msg.toStdString();
    const char* file = ctx.file ? ctx.file : "";
    const char* fn   = ctx.function ? ctx.function : "";

    switch (type) {
        case QtDebugMsg:    SPDLOG_DEBUG("[Qt] {} ({}:{} {})", m, file, ctx.line, fn); break;
        case QtInfoMsg:     SPDLOG_INFO ("[Qt] {} ({}:{} {})", m, file, ctx.line, fn); break;
        case QtWarningMsg:  SPDLOG_WARN ("[Qt] {} ({}:{} {})", m, file, ctx.line, fn); break;
        case QtCriticalMsg: SPDLOG_ERROR("[Qt] {} ({}:{} {})", m, file, ctx.line, fn);
                             CrashReporter::captureMessage("Qt critical: " + m); break;
        case QtFatalMsg:    SPDLOG_CRITICAL("[Qt FATAL] {} ({}:{} {})", m, file, ctx.line, fn);
                             CrashReporter::captureMessage("Qt fatal: " + m); break;
    }
}

void uploadPendingCrashes(const std::filesystem::path& dir) {
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return;
    for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const auto p = entry.path();
        if (p.extension() != ".dmp") continue;

        sentry_value_t event = sentry_value_new_event();
        sentry_value_set_by_key(event, "level", sentry_value_new_string("fatal"));
        sentry_value_set_by_key(event, "message",
            sentry_value_new_string(("Recovered crash dump: " + p.filename().string()).c_str()));

        // Tag with the dump filename so it can be correlated with the
        // Crashpad-managed minidump event (which carries the stack trace).
        sentry_value_t tags = sentry_value_new_object();
        sentry_value_set_by_key(tags, "crash_dump_file",
            sentry_value_new_string(p.filename().string().c_str()));
        sentry_value_set_by_key(event, "tags", tags);

        auto sidecar = std::filesystem::path(p).replace_extension(".json");
        if (std::filesystem::exists(sidecar, ec)) {
            std::ifstream f(sidecar);
            std::stringstream ss; ss << f.rdbuf();
            sentry_set_extra("state_snapshot", sentry_value_new_string(ss.str().c_str()));
        }

        sentry_capture_event(event);
        SPDLOG_INFO("CrashReporter: queued pending crash for upload: {}", p.string());

        // Move to .uploaded to avoid re-submission on next launch.
        std::filesystem::rename(p, p.string() + ".uploaded", ec);
        if (std::filesystem::exists(sidecar, ec)) {
            std::filesystem::rename(sidecar, sidecar.string() + ".uploaded", ec);
        }
    }
#else
    (void)dir;
#endif
}

} // namespace

bool CrashReporter::init(const Config& cfg) {
    auto& g = globals();
    if (g.initialized.load()) return true;

    g.config = cfg;
    std::error_code ec;
    if (!g.config.crashDir.empty()) {
        std::filesystem::create_directories(g.config.crashDir, ec);
    }
    if (!g.config.databaseDir.empty()) {
        std::filesystem::create_directories(g.config.databaseDir, ec);
    }

#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    if (!cfg.dsn.empty()) {
        sentry_options_t* options = sentry_options_new();
        sentry_options_set_dsn(options, cfg.dsn.c_str());
        if (!cfg.release.empty()) {
            sentry_options_set_release(options, cfg.release.c_str());
        }
        sentry_options_set_environment(options, cfg.environment.c_str());
        const double tracesSampleRate =
            parseSampleRate(std::getenv("MIB_SENTRY_TRACES_SAMPLE_RATE"),
                            cfg.tracesSampleRate);
        sentry_options_set_traces_sample_rate(options, tracesSampleRate);
        if (!cfg.databaseDir.empty()) {
            sentry_options_set_database_path(options, cfg.databaseDir.string().c_str());
        }
        sentry_options_set_auto_session_tracking(options, 1);
        sentry_options_set_symbolize_stacktraces(options, 1);

        if (sentry_init(options) != 0) {
            SPDLOG_WARN("CrashReporter: sentry_init failed; continuing local-only");
        } else {
            SPDLOG_INFO("CrashReporter: Sentry initialized (release={}, env={}, tracesSampleRate={})",
                        cfg.release, cfg.environment, tracesSampleRate);
        }
    } else {
        SPDLOG_INFO("CrashReporter: no DSN configured; running in local-only mode");
    }
#else
    SPDLOG_INFO("CrashReporter: built without Sentry support; running in local-only mode");
#endif

#ifdef _WIN32
    ::SetUnhandledExceptionFilter(sehHandler);
#endif

    if (cfg.installSignalHandlers) {
        std::signal(SIGSEGV, signalHandler);
        std::signal(SIGABRT, signalHandler);
        std::signal(SIGFPE,  signalHandler);
        std::signal(SIGILL,  signalHandler);
    }

    if (cfg.installTerminateHandler) {
        std::set_terminate(terminateHandler);
    }

    if (cfg.installQtMessageHandler) {
        qInstallMessageHandler(qtMessageHandler);
    }

    g.initialized.store(true);
    SPDLOG_INFO("CrashReporter initialized: crashDir={}", cfg.crashDir.string());

    if (cfg.uploadPendingOnStart) {
        uploadPendingCrashes(cfg.crashDir);
    }
    return true;
}

void CrashReporter::shutdown() {
    auto& g = globals();
    if (!g.initialized.load()) return;
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    sentry_close();
#endif
    g.initialized.store(false);
}

bool CrashReporter::isInitialized() {
    return globals().initialized.load();
}

void CrashReporter::setTag(std::string_view key, std::string_view value) {
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    if (!globals().initialized.load()) return;
    sentry_set_tag(std::string(key).c_str(), std::string(value).c_str());
#else
    (void)key; (void)value;
#endif
}

void CrashReporter::setContextJson(std::string_view name, std::string_view json) {
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    if (!globals().initialized.load()) return;
    sentry_value_t v = sentry_value_new_string(std::string(json).c_str());
    sentry_set_extra(std::string(name).c_str(), v);
#else
    (void)name; (void)json;
#endif
}

void CrashReporter::breadcrumb(std::string_view category,
                                std::string_view message,
                                std::string_view jsonData) {
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    if (!globals().initialized.load()) return;
    sentry_value_t crumb = sentry_value_new_breadcrumb(
        std::string(category).c_str(),
        std::string(message).c_str());
    if (!jsonData.empty()) {
        sentry_value_set_by_key(crumb, "data",
            sentry_value_new_string(std::string(jsonData).c_str()));
    }
    sentry_add_breadcrumb(crumb);
#else
    (void)category; (void)message; (void)jsonData;
#endif
}

void CrashReporter::registerStateMirror(StateSnapshotFn fn) {
    auto& g = globals();
    std::scoped_lock lk(g.stateSnapshotMutex);
    g.stateSnapshot = std::move(fn);
}

void CrashReporter::captureMessage(std::string_view message) {
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    if (!globals().initialized.load()) return;
    sentry_value_t ev = sentry_value_new_message_event(SENTRY_LEVEL_ERROR, "app",
                                                       std::string(message).c_str());
    sentry_capture_event(ev);
#else
    (void)message;
#endif
}

void CrashReporter::captureException(std::string_view what) {
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    if (!globals().initialized.load()) return;
    sentry_value_t ev = sentry_value_new_message_event(SENTRY_LEVEL_FATAL, "exception",
                                                       std::string(what).c_str());
    sentry_capture_event(ev);
#endif
    // Always write a local sidecar so we have something on disk even
    // without Sentry.
    try {
        const auto base = makeCrashFilenameBase(globals().config.crashDir, "exception");
        writeStateJsonSidecar(std::filesystem::path(base.string() + ".json"));

        // Also write a small text file with the exception message.
        std::ofstream f(base.string() + ".txt");
        f << "exception: " << what << "\n";
    } catch (...) {
    }
}

void CrashReporter::capturePerformanceTransaction(std::string_view name,
                                                  std::string_view operation,
                                                  double durationMs,
                                                  std::string_view jsonData) {
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    if (!globals().initialized.load()) return;
    if (name.empty() || operation.empty() || durationMs < 0.0) return;

    const std::string nameStr(name);
    const std::string opStr(operation);
    sentry_transaction_context_t* ctx =
        sentry_transaction_context_new(nameStr.c_str(), opStr.c_str());
    if (!ctx) return;

    const uint64_t finishUs = epochMicrosNow();
    const auto durationUs = static_cast<uint64_t>(durationMs * 1000.0);
    const uint64_t startUs = finishUs > durationUs ? finishUs - durationUs : finishUs;
    sentry_transaction_t* tx =
        sentry_transaction_start_ts(ctx, sentry_value_new_null(), startUs);
    if (!tx) return;

    sentry_transaction_set_tag(tx, "component", "mib-studio-qt");
    sentry_transaction_set_data(tx, "duration_ms", sentry_value_new_double(durationMs));
    if (!jsonData.empty()) {
        sentry_transaction_set_data(tx, "perf_data",
                                    sentry_value_new_string(std::string(jsonData).c_str()));
    }
    sentry_transaction_finish_ts(tx, finishUs);
#else
    (void)name;
    (void)operation;
    (void)durationMs;
    (void)jsonData;
#endif
}

bool CrashReporter::writeDiagnosticSnapshot(std::string_view reason) {
    if (!globals().initialized.load()) return false;
    try {
        const std::string r(reason.empty() ? std::string_view{"manual"} : reason);
        const auto base = makeCrashFilenameBase(globals().config.crashDir, r);
        writeStateJsonSidecar(std::filesystem::path(base.string() + ".json"));
        SPDLOG_INFO("CrashReporter: wrote diagnostic snapshot: {}", base.string());
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("CrashReporter: snapshot failed: {}", e.what());
        return false;
    }
}

[[noreturn]] void CrashReporter::triggerCrashForTesting(FaultKind kind) {
    SPDLOG_CRITICAL("CrashReporter: triggerCrashForTesting kind={}", static_cast<int>(kind));
    switch (kind) {
        case FaultKind::NullDeref: {
            volatile int* p = nullptr;
            *p = 0;
            break;
        }
        case FaultKind::Abort:
            std::abort();
        case FaultKind::Throw:
            throw std::runtime_error("triggerCrashForTesting(Throw)");
    }
    std::abort();
}

} // namespace backend::services
