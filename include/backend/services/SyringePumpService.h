#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class QByteArray;
class QSerialPort;

namespace backend::services {

class SyringePumpService {
public:
    enum class PumpId : int { Sample = 0, Sheath = 1 };
    static constexpr int PUMP_COUNT = 2;

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
        int comPort{-1};
        int baudRate{115200};
        uint8_t modbusAddress{1};
        double flowRate{0.0};
        uint16_t flowRateUnit{99};       // 99 = mL/min
        uint16_t syringeMfg{0};          // Syringe manufacturer index
        uint16_t syringeSpec{0};         // Syringe specification (volume) index
        Direction direction{Direction::Infuse};
    };

    struct PumpStatus {
        bool connected{false};
        RunStatus runStatus{RunStatus::Stop};
        double currentFlowRate{0.0};
        double accumulatedVolume{0.0};
        double minFlowRate{0.0};
        double maxFlowRate{0.0};
    };

    SyringePumpService();
    ~SyringePumpService();

    // Connection management
    bool connect(PumpId id, int comPort, int baudRate, uint8_t modbusAddress);
    void disconnect(PumpId id);
    bool isConnected(PumpId id) const;

    // Control
    bool setFlowRate(PumpId id, double rate, uint16_t unit);
    bool setDirection(PumpId id, Direction dir);
    bool start(PumpId id);
    bool stop(PumpId id);
    bool setSyringe(PumpId id, uint16_t manufacturer, uint16_t specification);

    // Status (thread-safe reads)
    PumpStatus getStatus(PumpId id) const;
    PumpConfig getConfig(PumpId id) const;
    void setConfig(PumpId id, const PumpConfig& config);

    // Poll current status from pump hardware (call from UI timer)
    void pollStatus(PumpId id);

    // Get COM port in use by a specific pump (for port collision avoidance)
    int getComPort(PumpId id) const;

private:
    // Modbus RTU helpers
    static uint16_t crc16(const uint8_t* data, size_t len);
    QByteArray buildReadRequest(uint8_t addr, uint16_t startReg, uint16_t count);
    QByteArray buildWriteSingleRequest(uint8_t addr, uint16_t reg, uint16_t value);
    QByteArray buildWriteMultipleRequest(uint8_t addr, uint16_t startReg, const QByteArray& regData);
    bool sendRequest(int pumpIdx, const QByteArray& request, QByteArray& response, int expectedBytes);

    // Float32 <-> register conversion (big-endian / ABCD word order)
    static QByteArray floatToRegisters(float value);
    static float registersToFloat(const uint8_t* data);

    // Read helpers
    bool readHoldingRegisters(int pumpIdx, uint16_t startReg, uint16_t count, QByteArray& data);
    bool writeSingleRegister(int pumpIdx, uint16_t reg, uint16_t value);
    bool writeMultipleRegisters(int pumpIdx, uint16_t startReg, const QByteArray& regData);

    struct PumpConnection {
        QSerialPort* serial{nullptr};
        PumpConfig config;
        PumpStatus status;
        mutable std::mutex mutex;
    };

    std::array<PumpConnection, PUMP_COUNT> pumps_;
};

} // namespace backend::services
