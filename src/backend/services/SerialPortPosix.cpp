// POSIX (termios) implementation of ISerialPort. Compiled on non-Windows
// builds (selected in src/backend/CMakeLists.txt). Qt-free.
#include "backend/services/ISerialPort.h"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace backend::services {

namespace {

speed_t baudConstant(int baud)
{
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default:     return B115200;
    }
}

// Best-effort COM-number -> device path for Linux USB-serial adapters (the
// dLSP pumps present as CDC/USB serial). Real deployments run on Windows; this
// keeps the Linux backend functional and is bypassed by the path test seam.
std::string devicePathForComPort(int comPort)
{
    return "/dev/ttyUSB" + std::to_string(comPort);
}

class PosixSerialPort final : public ISerialPort {
public:
    ~PosixSerialPort() override { close(); }

    bool open(int comPort, int baudRate) override
    {
        return openPath(devicePathForComPort(comPort), baudRate);
    }

    bool openPath(const std::string& devicePath, int baudRate)
    {
        close();
        fd_ = ::open(devicePath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0) {
            setErrno("open " + devicePath);
            return false;
        }

        termios tty{};
        if (::tcgetattr(fd_, &tty) != 0) {
            setErrno("tcgetattr");
            close();
            return false;
        }

        cfmakeraw(&tty); // 8-bit, no canonical processing, no echo
        // 8N1, no flow control, local + receiver enabled.
        tty.c_cflag &= ~static_cast<tcflag_t>(PARENB);   // no parity
        tty.c_cflag &= ~static_cast<tcflag_t>(CSTOPB);   // one stop bit
        tty.c_cflag &= ~static_cast<tcflag_t>(CSIZE);
        tty.c_cflag |= CS8;                              // 8 data bits
        tty.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);  // no hardware flow control
        tty.c_cflag |= (CLOCAL | CREAD);
        tty.c_iflag &= ~static_cast<tcflag_t>(IXON | IXOFF | IXANY); // no software flow control
        tty.c_cc[VMIN] = 0;  // non-blocking read; timeouts handled via select()
        tty.c_cc[VTIME] = 0;

        const speed_t speed = baudConstant(baudRate);
        cfsetispeed(&tty, speed);
        cfsetospeed(&tty, speed);

        if (::tcsetattr(fd_, TCSANOW, &tty) != 0) {
            setErrno("tcsetattr");
            close();
            return false;
        }
        ::tcflush(fd_, TCIOFLUSH);
        error_.clear();
        return true;
    }

    void close() override
    {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool isOpen() const override { return fd_ >= 0; }

    int write(const std::vector<uint8_t>& data) override
    {
        if (fd_ < 0) {
            error_ = "write on closed port";
            return -1;
        }
        size_t total = 0;
        while (total < data.size()) {
            const ssize_t n = ::write(fd_, data.data() + total, data.size() - total);
            if (n < 0) {
                if (errno == EAGAIN || errno == EINTR) {
                    continue;
                }
                setErrno("write");
                return -1;
            }
            total += static_cast<size_t>(n);
        }
        return static_cast<int>(total);
    }

    bool waitForBytesWritten(int /*timeoutMs*/) override
    {
        if (fd_ < 0) return false;
        // Block until the kernel has transmitted queued output. tcdrain has no
        // timeout parameter; for a real device or a pty it returns promptly.
        return ::tcdrain(fd_) == 0;
    }

    bool waitForReadyRead(int timeoutMs) override
    {
        if (fd_ < 0) return false;
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(fd_, &rset);
        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        const int rc = ::select(fd_ + 1, &rset, nullptr, nullptr, &tv);
        return rc > 0 && FD_ISSET(fd_, &rset);
    }

    std::vector<uint8_t> readAll() override
    {
        std::vector<uint8_t> out;
        if (fd_ < 0) return out;
        uint8_t buf[512];
        for (;;) {
            const ssize_t n = ::read(fd_, buf, sizeof(buf));
            if (n > 0) {
                out.insert(out.end(), buf, buf + n);
                if (static_cast<size_t>(n) < sizeof(buf)) break;
            } else if (n < 0 && errno == EINTR) {
                continue;
            } else {
                break; // 0 (EOF for now) or EAGAIN (no more data)
            }
        }
        return out;
    }

    std::string lastError() const override { return error_; }

private:
    void setErrno(const std::string& what)
    {
        error_ = what + ": " + std::strerror(errno);
    }

    int fd_{-1};
    std::string error_;
};

} // namespace

std::unique_ptr<ISerialPort> makePlatformSerialPort()
{
    return std::make_unique<PosixSerialPort>();
}

std::unique_ptr<ISerialPort> makeSerialPortForPathForTesting(const std::string& devicePath,
                                                             int baudRate)
{
    auto port = std::make_unique<PosixSerialPort>();
    if (!port->openPath(devicePath, baudRate)) {
        SPDLOG_WARN("makeSerialPortForPathForTesting: failed to open {}: {}",
                    devicePath, port->lastError());
        return nullptr;
    }
    return port;
}

} // namespace backend::services
