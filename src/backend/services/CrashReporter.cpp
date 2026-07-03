#include "backend/services/CrashReporter.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
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
    // True once sentry_init succeeded — the Sentry backend (crashpad on
    // Windows, inproc elsewhere) then owns the process crash handlers.
    std::atomic<bool> sentryActive{false};
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
        // try_lock so we never deadlock if the crashing thread holds the
        // snapshot mutex; on contention we still read the callback without
        // the lock (best-effort — the mirror is set once at startup).
        std::unique_lock<std::mutex> lk(g.stateSnapshotMutex, std::try_to_lock);
        if (g.stateSnapshot) {
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
    // std::terminate runs on a normal thread (not from a signal), so
    // describing the active exception and flushing Sentry is safe here.
    std::string what = "std::terminate without active exception";
    if (auto ex = std::current_exception()) {
        try {
            std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            what = std::string("uncaught exception: ") + e.what();
        } catch (...) {
            what = "uncaught non-std exception";
        }
    }
    try {
        SPDLOG_CRITICAL("terminateHandler: {}", what);
        std::error_code ec;
        std::filesystem::create_directories(g.config.crashDir, ec);
        const auto base = makeCrashFilenameBase(g.config.crashDir, "terminate");
        const auto json = std::filesystem::path(base.string() + ".json");
        writeStateJsonSidecar(json);
        std::ofstream txt(base.string() + ".txt");
        txt << what << "\n";
#ifdef _WIN32
        // Only write a local dump when Sentry is not live — otherwise the
        // abort() below reaches Crashpad, which captures its own dump, and
        // this one would be re-uploaded next launch as a duplicate.
        if (!g.sentryActive.load()) {
            const auto dump = std::filesystem::path(base.string() + ".dmp");
            writeMinidumpInternal(nullptr, dump);
        }
#endif
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
        if (g.sentryActive.load()) {
            sentry_value_t ev = sentry_value_new_message_event(
                SENTRY_LEVEL_FATAL, "terminate", what.c_str());
            sentry_capture_event(ev);
            sentry_flush(2000);
        }
#endif
    } catch (...) {
    }
    std::abort();
}

#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
// Invoked by the Sentry backend from its own crash handler (crashpad's
// local exception handler on Windows, the inproc signal handler elsewhere)
// so the local .dmp/.json sidecars are still produced while Sentry keeps
// ownership of the process crash handlers. Returning the event unchanged
// lets Sentry upload it.
sentry_value_t sentryOnCrashHook(const sentry_ucontext_t* uctx,
                                 sentry_value_t event,
                                 void* /*closure*/) {
    auto& g = globals();
    bool expected = false;
    if (g.handlingCrash.compare_exchange_strong(expected, true)) {
        try {
            std::error_code ec;
            std::filesystem::create_directories(g.config.crashDir, ec);
            const auto base = makeCrashFilenameBase(g.config.crashDir, "crash");
#ifdef _WIN32
            if (uctx) {
                EXCEPTION_POINTERS eptr = uctx->exception_ptrs;
                writeMinidumpInternal(&eptr, std::filesystem::path(base.string() + ".dmp"));
            }
#else
            (void)uctx;
#endif
            writeStateJsonSidecar(std::filesystem::path(base.string() + ".json"));
        } catch (...) {
            // Already crashing — never throw back into the crash handler.
        }
    }
    return event;
}
#endif // MIB_USE_SENTRY

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
                             CrashReporter::captureMessage("Qt fatal: " + m);
                             // Qt aborts right after this handler returns, so
                             // the event must be flushed now or it is lost.
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
                             if (globals().sentryActive.load()) sentry_flush(2000);
#endif
                             break;
    }
}

bool stemEndsWith(const std::filesystem::path& p, std::string_view suffix) {
    const std::string stem = p.stem().string();
    return stem.size() >= suffix.size() &&
           stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void uploadPendingCrashes(const std::filesystem::path& dir) {
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    if (!globals().sentryActive.load()) return;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return;
    for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const auto p = entry.path();
        if (p.extension() != ".dmp") continue;

        const auto sidecar = std::filesystem::path(p).replace_extension(".json");
        const bool haveSidecar = std::filesystem::exists(sidecar, ec);

        // "-crash" dumps were written by the on_crash hook while the Sentry
        // backend was live, so Crashpad/inproc already reported that crash.
        // Re-capturing them would duplicate the event; just retire them.
        if (!stemEndsWith(p, "-crash")) {
            if (haveSidecar) {
                std::ifstream f(sidecar);
                std::stringstream ss; ss << f.rdbuf();
                sentry_set_extra("state_snapshot",
                                 sentry_value_new_string(ss.str().c_str()));
            }
            // Uploads the minidump itself so the crash gets a real stack
            // trace, not just a "recovered dump" message.
            sentry_capture_minidump(p.string().c_str());
            if (haveSidecar) {
                sentry_remove_extra("state_snapshot");
            }
            SPDLOG_INFO("CrashReporter: uploaded pending crash dump: {}", p.string());
        }

        // Move to .uploaded to avoid re-submission on next launch.
        std::filesystem::rename(p, p.string() + ".uploaded", ec);
        if (haveSidecar) {
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
#ifdef _WIN32
        if (!cfg.handlerPath.empty()) {
            // Pin crashpad_handler.exe explicitly instead of relying on
            // sentry-native's implicit lookup (which can miss when the
            // working directory differs from the install directory).
            sentry_options_set_handler_pathw(options, cfg.handlerPath.c_str());
        }
#endif
        sentry_options_set_auto_session_tracking(options, 1);
        sentry_options_set_symbolize_stacktraces(options, 1);
        // Local .dmp/.json sidecars are produced from Sentry's own crash
        // handler so we never have to install a competing SEH/signal
        // handler on top of it.
        sentry_options_set_on_crash(options, sentryOnCrashHook, nullptr);

        if (sentry_init(options) != 0) {
            SPDLOG_WARN("CrashReporter: sentry_init failed; continuing local-only");
        } else {
            g.sentryActive.store(true);
            SPDLOG_INFO("CrashReporter: Sentry initialized (release={}, env={}, tracesSampleRate={})",
                        cfg.release, cfg.environment, tracesSampleRate);
        }
    } else {
        SPDLOG_INFO("CrashReporter: no DSN configured; running in local-only mode");
    }
#else
    SPDLOG_INFO("CrashReporter: built without Sentry support; running in local-only mode");
#endif

    // Install our own crash handlers ONLY when the Sentry backend is not
    // live. sentry_init() installs the Crashpad (Windows) / inproc (POSIX)
    // handlers, and SetUnhandledExceptionFilter / std::signal REPLACE the
    // installed handler without chaining — overriding them here used to
    // disable Sentry crash capture entirely. When Sentry is live, the local
    // sidecars come from sentryOnCrashHook instead.
    if (!g.sentryActive.load()) {
#ifdef _WIN32
        ::SetUnhandledExceptionFilter(sehHandler);
#endif
        if (cfg.installSignalHandlers) {
            std::signal(SIGSEGV, signalHandler);
            std::signal(SIGABRT, signalHandler);
            std::signal(SIGFPE,  signalHandler);
            std::signal(SIGILL,  signalHandler);
        }
    }

    if (cfg.installTerminateHandler) {
        std::set_terminate(terminateHandler);
    }

    g.initialized.store(true);

    if (cfg.installQtMessageHandler) {
        qInstallMessageHandler(qtMessageHandler);
    }

    SPDLOG_INFO("CrashReporter initialized: crashDir={}, sentryActive={}",
                cfg.crashDir.string(), g.sentryActive.load());

    if (cfg.uploadPendingOnStart) {
        uploadPendingCrashes(cfg.crashDir);
    }
    return true;
}

void CrashReporter::shutdown() {
    auto& g = globals();
    if (!g.initialized.load()) return;
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    if (g.sentryActive.load()) {
        sentry_close(); // flushes pending events
        g.sentryActive.store(false);
    }
#endif
    g.initialized.store(false);
}

bool CrashReporter::isInitialized() {
    return globals().initialized.load();
}

void CrashReporter::setTag(std::string_view key, std::string_view value) {
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    if (!globals().sentryActive.load()) return;
    sentry_set_tag(std::string(key).c_str(), std::string(value).c_str());
#else
    (void)key; (void)value;
#endif
}

void CrashReporter::setContextJson(std::string_view name, std::string_view json) {
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    if (!globals().sentryActive.load()) return;
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
    if (!globals().sentryActive.load()) return;
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
    if (!globals().sentryActive.load()) return;
    sentry_value_t ev = sentry_value_new_message_event(SENTRY_LEVEL_ERROR, "app",
                                                       std::string(message).c_str());
    sentry_capture_event(ev);
#else
    (void)message;
#endif
}

void CrashReporter::captureException(std::string_view what) {
#if defined(MIB_USE_SENTRY) && MIB_USE_SENTRY
    if (globals().sentryActive.load()) {
        sentry_value_t ev = sentry_value_new_message_event(SENTRY_LEVEL_FATAL, "exception",
                                                           std::string(what).c_str());
        sentry_capture_event(ev);
        // Callers are usually about to die (top-level catch in main); the
        // event is lost unless it is flushed before the process exits.
        sentry_flush(2000);
    }
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
    if (!globals().sentryActive.load()) return;
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
