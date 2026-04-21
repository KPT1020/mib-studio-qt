#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <QString>
#include <vector>

class QByteArray;
class QSerialPort;

namespace backend::services {

class SyringePumpService {
public:
    using PumpHandle = uint64_t;

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
        QString name{"Pump"};
        QString portName{};
        int baudRate{115200};
        uint8_t modbusAddress{1};
        double flowRate{0.0};
        uint16_t flowRateUnit{100};      // 100 = µL/min
        Direction direction{Direction::Infuse};
        uint16_t syringeVolume{100};
        uint16_t syringeVolumeUnit{100};
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

    // Pump lifecycle
    PumpHandle addPump(const QString& name);
    bool removePump(PumpHandle handle);
    void clearPumps();
    std::vector<PumpHandle> pumpHandles() const;
    size_t pumpCount() const;
    bool hasPump(PumpHandle handle) const;

    // Connection management
    bool connect(PumpHandle handle, const QString& portName, int baudRate, uint8_t modbusAddress);
#ifdef _WIN32
    // Windows-only compatibility API.
    bool connect(PumpHandle handle, int comPort, int baudRate, uint8_t modbusAddress);
#endif
    void disconnect(PumpHandle handle);
    bool isConnected(PumpHandle handle) const;

    // Control
    bool setFlowRate(PumpHandle handle, double rate, uint16_t unit);
    bool setDirection(PumpHandle handle, Direction dir);
    bool start(PumpHandle handle);
    bool stop(PumpHandle handle);
    bool purge(PumpHandle handle, Direction dir);
    bool stopPurge(PumpHandle handle);
    bool setSyringeVolume(PumpHandle handle, uint16_t volume, uint16_t unit);

    // Status (thread-safe reads)
    PumpStatus getStatus(PumpHandle handle) const;
    PumpConfig getConfig(PumpHandle handle) const;
    void setConfig(PumpHandle handle, const PumpConfig& config);
    QString getPumpName(PumpHandle handle) const;
    void setPumpName(PumpHandle handle, const QString& name);

    // Poll current status from pump hardware (call from UI timer)
    void pollStatus(PumpHandle handle);

    // Get serial port in use by a specific pump (for port collision avoidance)
    QString getPortName(PumpHandle handle) const;
#ifdef _WIN32
    // Windows-only compatibility API.
    int getComPort(PumpHandle handle) const;
#endif

private:
    // Modbus RTU helpers
    static uint16_t crc16(const uint8_t* data, size_t len);
    QByteArray buildReadRequest(uint8_t addr, uint16_t startReg, uint16_t count);
    QByteArray buildWriteSingleRequest(uint8_t addr, uint16_t reg, uint16_t value);
    QByteArray buildWriteMultipleRequest(uint8_t addr, uint16_t startReg, const QByteArray& regData);
    struct PumpConnection;
    bool sendRequest(const PumpConnection& pump, const QByteArray& request, QByteArray& response, int expectedBytes);

    // Float32 <-> register conversion (big-endian / ABCD word order)
    static QByteArray floatToRegisters(float value);
    static float registersToFloat(const uint8_t* data);

    // Read helpers
    bool readHoldingRegisters(const PumpConnection& pump, uint16_t startReg, uint16_t count, QByteArray& data);
    bool writeSingleRegister(const PumpConnection& pump, uint16_t reg, uint16_t value);
    bool writeMultipleRegisters(const PumpConnection& pump, uint16_t startReg, const QByteArray& regData);

    struct PumpConnection {
        PumpHandle handle{0};
        QSerialPort* serial{nullptr};
        PumpConfig config;
        PumpStatus status;
        mutable std::mutex mutex;
    };

    std::shared_ptr<PumpConnection> getPump(PumpHandle handle) const;

    mutable std::mutex pumpsMutex_;
    std::vector<std::shared_ptr<PumpConnection>> pumps_;
    std::atomic<PumpHandle> nextHandle_{1};
};

} // namespace backend::services
