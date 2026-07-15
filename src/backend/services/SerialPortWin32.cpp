// Win32 implementation of ISerialPort. Compiled on Windows only (selected in
// src/backend/CMakeLists.txt). Qt-free.
#include "backend/services/ISerialPort.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <string>
#include <thread>

namespace backend::services {

namespace {

class Win32SerialPort final : public ISerialPort {
public:
    ~Win32SerialPort() override { close(); }

    bool open(int comPort, int baudRate) override
    {
        close();
        // \\.\COMn form is required for COM10 and above.
        const std::string path = "\\\\.\\COM" + std::to_string(comPort);
        handle_ = ::CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            setLastError("CreateFile " + path);
            return false;
        }

        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        if (!::GetCommState(handle_, &dcb)) {
            setLastError("GetCommState");
            close();
            return false;
        }
        dcb.BaudRate = static_cast<DWORD>(baudRate);
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_DISABLE;
        dcb.fRtsControl = RTS_CONTROL_DISABLE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        if (!::SetCommState(handle_, &dcb)) {
            setLastError("SetCommState");
            close();
            return false;
        }

        // Non-blocking reads: return immediately with whatever is queued; the
        // service layer polls via waitForReadyRead().
        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutConstant = 0;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 1000;
        timeouts.WriteTotalTimeoutMultiplier = 0;
        if (!::SetCommTimeouts(handle_, &timeouts)) {
            setLastError("SetCommTimeouts");
            close();
            return false;
        }
        ::PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);
        error_.clear();
        return true;
    }

    void close() override
    {
        if (handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    bool isOpen() const override { return handle_ != INVALID_HANDLE_VALUE; }

    int write(const std::vector<uint8_t>& data) override
    {
        if (handle_ == INVALID_HANDLE_VALUE) {
            error_ = "write on closed port";
            return -1;
        }
        DWORD written = 0;
        if (!::WriteFile(handle_, data.data(), static_cast<DWORD>(data.size()),
                         &written, nullptr)) {
            setLastError("WriteFile");
            return -1;
        }
        return static_cast<int>(written);
    }

    bool waitForBytesWritten(int /*timeoutMs*/) override
    {
        if (handle_ == INVALID_HANDLE_VALUE) return false;
        return ::FlushFileBuffers(handle_) != 0;
    }

    bool waitForReadyRead(int timeoutMs) override
    {
        if (handle_ == INVALID_HANDLE_VALUE) return false;
        // Poll the input queue up to the timeout.
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(timeoutMs);
        for (;;) {
            if (queuedBytes() > 0) return true;
            if (std::chrono::steady_clock::now() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::vector<uint8_t> readAll() override
    {
        std::vector<uint8_t> out;
        if (handle_ == INVALID_HANDLE_VALUE) return out;
        const DWORD avail = queuedBytes();
        if (avail == 0) return out;
        out.resize(avail);
        DWORD read = 0;
        if (!::ReadFile(handle_, out.data(), avail, &read, nullptr)) {
            setLastError("ReadFile");
            out.clear();
            return out;
        }
        out.resize(read);
        return out;
    }

    std::string lastError() const override { return error_; }

private:
    DWORD queuedBytes()
    {
        COMSTAT stat{};
        DWORD errors = 0;
        if (!::ClearCommError(handle_, &errors, &stat)) {
            return 0;
        }
        return stat.cbInQue;
    }

    void setLastError(const std::string& what)
    {
        error_ = what + " failed (GetLastError=" + std::to_string(::GetLastError()) + ")";
    }

    HANDLE handle_{INVALID_HANDLE_VALUE};
    std::string error_;
};

} // namespace

std::unique_ptr<ISerialPort> makePlatformSerialPort()
{
    return std::make_unique<Win32SerialPort>();
}

std::unique_ptr<ISerialPort> makeSerialPortForPathForTesting(const std::string& /*devicePath*/,
                                                             int /*baudRate*/)
{
    // Path-based (pty) opening is a POSIX-only test seam.
    return nullptr;
}

} // namespace backend::services
