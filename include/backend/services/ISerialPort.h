// UI-neutral serial transport for the syringe-pump Modbus RTU I/O. Replaces the
// direct QSerialPort dependency so the backend links no Qt SerialPort (epic
// #246). Fixed 8 data bits / no parity / 1 stop bit / no flow control — the
// only mode the dLSP pumps use. Payloads are byte vectors (the Modbus framing
// layer in ModbusRtu.h is already vector-based), so this contract is Qt-free.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace backend::services {

class ISerialPort {
public:
    virtual ~ISerialPort() = default;

    // Open COM<comPort> at baudRate with 8N1 / no flow control. Returns false
    // on failure; lastError() describes why.
    virtual bool open(int comPort, int baudRate) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // Write all bytes. Returns the number written, or -1 on error.
    virtual int write(const std::vector<uint8_t>& data) = 0;

    // Block until queued output has been transmitted, or timeoutMs elapses.
    virtual bool waitForBytesWritten(int timeoutMs) = 0;
    // Block until at least one byte is available to read, or timeoutMs elapses.
    virtual bool waitForReadyRead(int timeoutMs) = 0;
    // Return everything currently available (possibly empty).
    virtual std::vector<uint8_t> readAll() = 0;

    virtual std::string lastError() const = 0;
};

using SerialPortFactory = std::function<std::unique_ptr<ISerialPort>()>;

// Construct the platform serial port (POSIX termios or Win32), selected at
// build time by which SerialPort*.cpp is compiled.
std::unique_ptr<ISerialPort> makePlatformSerialPort();

// Test seam: open an explicit device path (e.g. a pty slave) instead of a
// COM-number mapping. POSIX builds return an opened port; other platforms
// return nullptr. Used by the loopback test to exercise the real transport.
std::unique_ptr<ISerialPort> makeSerialPortForPathForTesting(const std::string& devicePath,
                                                             int baudRate);

} // namespace backend::services
