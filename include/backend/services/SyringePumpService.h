#pragma once

#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class QByteArray;
class QSerialPort;

namespace backend::services {

class SyringePumpService {
public:
    using PumpHandle = int;
    static constexpr PumpHandle InvalidHandle = -1;

    enum class RunStatus : uint16_t {
        Stop     = 0,
        Forward  = 1,
        Backward = 2,
        Pause    = 3
    };

    enum class Direction : uint16_t {
        Infuse   = 0,
        Withdraw = 1
    };

    struct PumpConfig {
        QString portName;                 // cross-platform ("COM3", "/dev/ttyUSB0", ...)
        int baudRate{115200};
        uint8_t modbusAddress{1};
        double flowRate{0.0};
        uint16_t flowRateUnit{100};       // 100 = µL/min
        Direction direction{Direction::Infuse};
        uint16_t syringeVolume{10};
        uint16_t syringeVolumeUnit{103};  // 103 = mL
    };

    struct PumpStatus {
        bool connected{false};
        RunStatus runStatus{RunStatus::Stop};
        double currentFlowRate{0.0};
        double accumulatedVolume{0.0};
        double minFlowRate{0.0};
        double maxFlowRate{0.0};
        bool stalled{false};
    };

    SyringePumpService();
    ~SyringePumpService();

    // ------------------------------------------------------------------
    // Pump lifecycle (dynamic N-pump model)
    // ------------------------------------------------------------------
    PumpHandle addPump(const std::string& name);
    bool removePump(PumpHandle id);                           // disconnects first
    int pumpCount() const;
    std::vector<PumpHandle> pumpHandles() const;              // ordered snapshot
    std::string pumpName(PumpHandle id) const;
    void setPumpName(PumpHandle id, const std::string& name);

    // ------------------------------------------------------------------
    // Connection management (cross-platform)
    // ------------------------------------------------------------------
    bool connect(PumpHandle id, const QString& portName, int baudRate, uint8_t modbusAddress);
    // Windows convenience wrapper; on other platforms the int `comPort` is
    // stringified as "COM<n>" (not generally useful outside Windows).
    bool connect(PumpHandle id, int comPort, int baudRate, uint8_t modbusAddress);
    void disconnect(PumpHandle id);
    bool isConnected(PumpHandle id) const;

    // ------------------------------------------------------------------
    // Control
    // ------------------------------------------------------------------
    bool setFlowRate(PumpHandle id, double rate, uint16_t unit);
    bool setDirection(PumpHandle id, Direction dir);
    bool start(PumpHandle id);
    bool stop(PumpHandle id);
    bool purge(PumpHandle id, Direction dir);
    bool stopPurge(PumpHandle id);
    bool setSyringeVolume(PumpHandle id, uint16_t volume, uint16_t unit);

    // ------------------------------------------------------------------
    // Status / config (thread-safe reads)
    // ------------------------------------------------------------------
    PumpStatus getStatus(PumpHandle id) const;
    PumpConfig getConfig(PumpHandle id) const;
    void setConfig(PumpHandle id, const PumpConfig& config);

    void pollStatus(PumpHandle id);

    // Port in use by a specific pump (empty if disconnected / invalid).
    QString getPortName(PumpHandle id) const;
    // Port names in use by all currently-connected pumps (optionally
    // excluding one handle — useful when a settings UI wants to show
    // "other pumps' ports").
    QStringList reservedPortNames(PumpHandle exclude = InvalidHandle) const;

private:
    // Modbus RTU helpers
    static uint16_t crc16(const uint8_t* data, size_t len);
    QByteArray buildReadRequest(uint8_t addr, uint16_t startReg, uint16_t count);
    QByteArray buildWriteSingleRequest(uint8_t addr, uint16_t reg, uint16_t value);
    QByteArray buildWriteMultipleRequest(uint8_t addr, uint16_t startReg, const QByteArray& regData);

    // Float32 <-> register conversion (big-endian / ABCD word order)
    static QByteArray floatToRegisters(float value);
    static float registersToFloat(const uint8_t* data);

    struct Pump {
        std::string name;
        QSerialPort* serial{nullptr};
        PumpConfig config;
        PumpStatus status;
        mutable std::mutex mutex;
    };

    // Serial send/receive helpers — operate on a concrete Pump*.
    bool sendRequest(Pump& pump, const QByteArray& request,
                     QByteArray& response, int expectedBytes);
    bool readHoldingRegisters(Pump& pump, uint16_t startReg,
                              uint16_t count, QByteArray& data);
    bool writeSingleRegister(Pump& pump, uint16_t reg, uint16_t value);
    bool writeMultipleRegisters(Pump& pump, uint16_t startReg,
                                const QByteArray& regData);

    // Thread-safe lookup; returns nullptr for unknown handles. The returned
    // shared_ptr keeps the Pump alive for the duration of the caller's use,
    // even if removePump races with operations on another thread.
    std::shared_ptr<Pump> findPump(PumpHandle id) const;

    mutable std::mutex pumpsMutex_;
    std::map<PumpHandle, std::shared_ptr<Pump>> pumps_;
    PumpHandle nextHandle_{0};
};

} // namespace backend::services
