#include "backend/diagnostics/StartupProbe.h"

#include <cerrno>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace backend::diagnostics {
namespace {

constexpr const char* kMarkerFileName = "startup.inprogress";

long currentPid()
{
#ifdef _WIN32
    return static_cast<long>(GetCurrentProcessId());
#else
    return static_cast<long>(getpid());
#endif
}

// Best-effort liveness check. A recycled pid can alias a new process, so a
// rare false "alive" (previous failure not reported) is possible; a false
// "dead" is not, which is the direction that matters for not spamming users
// who run two instances at once.
bool isProcessAlive(long pid)
{
    if (pid <= 0) return false;
#ifdef _WIN32
    HANDLE handle =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (handle == nullptr) {
        // Access denied means the pid exists but is protected.
        return GetLastError() == ERROR_ACCESS_DENIED;
    }
    DWORD exitCode = 0;
    const bool alive = GetExitCodeProcess(handle, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(handle);
    return alive;
#else
    if (kill(static_cast<pid_t>(pid), 0) == 0) return true;
    return errno == EPERM;
#endif
}

std::string utcNowIso()
{
    const std::time_t now =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) return {};
    return buf;
}

} // namespace

StartupProbe::StartupProbe(long pidForTesting)
    : pid_(pidForTesting > 0 ? pidForTesting : currentPid())
{
}

StartupProbe::PreviousAttempt StartupProbe::begin(const std::filesystem::path& markerDir,
                                                  const std::string& appVersion)
{
    PreviousAttempt previous;
    try {
        std::error_code ec;
        std::filesystem::create_directories(markerDir, ec);
        markerPath_ = markerDir / kMarkerFileName;
        appVersion_ = appVersion;

        if (std::filesystem::exists(markerPath_, ec)) {
            std::ifstream in(markerPath_);
            std::string line;
            std::ostringstream all;
            long markerPid = -1;
            while (std::getline(in, line)) {
                if (line.empty()) continue;
                all << line << '\n';
                if (line.rfind("stage=", 0) == 0) {
                    previous.stage = line.substr(6);
                } else if (line.rfind("pid=", 0) == 0) {
                    try {
                        markerPid = std::stol(line.substr(4));
                    } catch (...) {
                        markerPid = -1;
                    }
                }
            }
            if (!isProcessAlive(markerPid)) {
                previous.found = true;
                previous.detail = all.str();
            }
        }

        active_ = true;
        currentStage_ = "begin";
        write();
    } catch (...) {
        // Diagnostics must never break startup.
        active_ = false;
    }
    return previous;
}

void StartupProbe::stage(const std::string& name)
{
    if (!active_) return;
    currentStage_ = name;
    write();
}

void StartupProbe::complete()
{
    if (!active_) return;
    std::error_code ec;
    std::filesystem::remove(markerPath_, ec);
    active_ = false;
    currentStage_.clear();
}

void StartupProbe::write()
{
    try {
        std::ofstream out(markerPath_, std::ios::trunc);
        if (!out.is_open()) return;
        out << "stage=" << currentStage_ << '\n'
            << "version=" << appVersion_ << '\n'
            << "pid=" << pid_ << '\n'
            << "utc=" << utcNowIso() << '\n';
        out.flush();
    } catch (...) {
        // Ignore: losing a marker update is acceptable, breaking boot is not.
    }
}

const char* StartupProbe::markerFileName()
{
    return kMarkerFileName;
}

} // namespace backend::diagnostics
