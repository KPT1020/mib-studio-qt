#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

class QSerialPort;

namespace backend::services::serialbus {

/**
 * Shared RS485/Modbus RTU bus layer.
 *
 * RS485 is a multi-drop bus: one physical USB/RS485 adapter can carry several
 * Modbus RTU devices at different slave addresses, so the QSerialPort handle
 * must be owned once per adapter and shared by every service that talks on it.
 * SerialBusManager hands out one ModbusBusSession per (system port name,
 * serial settings); the session serializes all transactions (inter-frame
 * delay + strict request/response correlation) so concurrent clients can
 * never cross-associate responses.
 */

// Serial line settings that identify a bus configuration. Two clients may
// share one adapter only when every field matches — a mismatch is a
// misconfiguration, not a second session.
struct SerialSettings {
    int baudRate{9600};
    int dataBits{8};   // 5..8
    char parity{'N'};  // 'N' none, 'E' even, 'O' odd
    int stopBits{1};   // 1 or 2

    bool operator==(const SerialSettings& o) const
    {
        return baudRate == o.baudRate && dataBits == o.dataBits &&
               parity == o.parity && stopBits == o.stopBits;
    }
    bool operator!=(const SerialSettings& o) const { return !(*this == o); }
};

// One enumerated serial port with the USB identity fields (when present) that
// survive a device-node rename across reboots/replug.
struct PortInfo {
    QString systemName;     // "ttyUSB0", "COM3"
    QString systemLocation; // "/dev/ttyUSB0", "\\\\.\\COM3"
    QString description;
    QString manufacturer;
    QString serialNumber;
    uint16_t vendorId{0};
    uint16_t productId{0};
};

// Cross-platform enumeration via QSerialPortInfo. An explicit call each time —
// no caching — so a Refresh action in the GUI always reflects hot-plug state.
std::vector<PortInfo> availablePorts();

enum class BusError {
    None,
    PortUnavailable,   // port does not exist / cannot be opened
    PortBusy,          // held by another process, or by us with other settings
    NotOpen,           // session lost its port
    WriteFailed,
    Timeout,           // no (matching) response before the deadline
    CrcError,          // corrupt frame — possible duplicate-address collision
    FrameError,        // unframeable bytes — possible collision or wrong baud
    WrongAddress,      // only frames from other slave addresses arrived
    WrongFunction,     // addressed device answered a different function code
    ModbusException,   // addressed device answered with a Modbus exception
    CollisionSuspected // extra bytes after a valid frame — duplicate address?
};

const char* toString(BusError error);

struct Transaction {
    BusError error{BusError::Timeout};
    uint8_t exceptionCode{0}; // valid when error == ModbusException
    QByteArray response;      // full validated frame when error is None/ModbusException
};

class SerialBusManager;

// Exclusive owner of one QSerialPort. Construct only through
// SerialBusManager::acquire(); hold via shared_ptr — the port closes when the
// last client releases its reference.
class ModbusBusSession {
public:
    ~ModbusBusSession();

    ModbusBusSession(const ModbusBusSession&) = delete;
    ModbusBusSession& operator=(const ModbusBusSession&) = delete;

    // One serialized Modbus RTU transaction: drains stale bytes, enforces the
    // inter-frame delay, writes `request` (byte 0 = slave address) and reads
    // exactly one strictly-correlated response frame. Complete frames from a
    // different slave address (stale/delayed traffic) are discarded and the
    // read continues until the deadline; corrupt or unframeable bytes fail
    // the transaction rather than being reinterpreted.
    Transaction transact(const QByteArray& request, int timeoutMs = 1000);

    QString portName() const { return portName_; }
    SerialSettings settings() const { return settings_; }
    bool isOpen() const;

private:
    friend class SerialBusManager;
    ModbusBusSession(SerialBusManager* manager, const QString& portName,
                     const SerialSettings& settings);
    bool open(QString* errorDetail);

    SerialBusManager* manager_;
    QString portName_;
    SerialSettings settings_;
    QSerialPort* serial_{nullptr};
    mutable std::mutex ioMutex_;
};

// Process-wide registry: one live session per system port. acquire() returns
// the existing session when the settings match, refuses with PortBusy when the
// port is already held with different settings, and opens the port otherwise.
class SerialBusManager {
public:
    SerialBusManager() = default;
    ~SerialBusManager() = default;

    SerialBusManager(const SerialBusManager&) = delete;
    SerialBusManager& operator=(const SerialBusManager&) = delete;

    std::shared_ptr<ModbusBusSession> acquire(const QString& portName,
                                              const SerialSettings& settings,
                                              BusError* error = nullptr,
                                              QString* errorDetail = nullptr);

private:
    friend class ModbusBusSession;
    void release(const QString& key);
    static QString normalizeKey(const QString& portName);

    std::mutex mutex_;
    std::map<QString, std::weak_ptr<ModbusBusSession>> sessions_;
};

} // namespace backend::services::serialbus
