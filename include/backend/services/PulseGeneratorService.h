#pragma once

#include <array>
#include <cstdint>
#include <mutex>

#include <QByteArray>

class QSerialPort;

namespace backend::services {

/**
 * Controls a Zhongsheng (中盛科技) pulse frequency & duty-cycle output module
 * over RS485 Modbus RTU. The module generates the TTL pulse train used as the
 * camera's external acquisition-trigger source (NOT the sort-output pulse —
 * see TriggerService for that).
 *
 * Protocol (vendor manual 脉冲频率与占空比输出系列 V2.0):
 *  - Modbus RTU, default address 1, 9600 8N1.
 *  - Per channel N (0-based): frequency as u32 = Hz*100 across holding
 *    registers 3N (high word) and 3N+1 (low word); duty as u16 = %*100 at
 *    register 3N+2.
 *  - Output range 400 Hz – 40 kHz, duty 0–100 %, resolution 0.01 Hz / 0.01 %.
 *  - There is no run/stop register: the pulse train is gated by writing duty
 *    0 % (line idles low) and restoring the configured duty to resume.
 */
class PulseGeneratorService {
public:
    static constexpr int CHANNEL_COUNT = 4;
    static constexpr double MIN_FREQUENCY_HZ = 400.0;
    static constexpr double MAX_FREQUENCY_HZ = 40000.0;

    struct Config {
        int comPort{-1};
        int baudRate{9600};
        uint8_t modbusAddress{1};
    };

    struct ChannelState {
        double frequencyHz{0.0};    // last written/read frequency
        double dutyPercent{0.0};    // configured duty (restored on enable)
        bool outputEnabled{false};  // false = duty 0 written to the device
    };

    struct Status {
        bool connected{false};
        std::array<ChannelState, CHANNEL_COUNT> channels{};
    };

    PulseGeneratorService();
    ~PulseGeneratorService();

    PulseGeneratorService(const PulseGeneratorService&) = delete;
    PulseGeneratorService& operator=(const PulseGeneratorService&) = delete;

    // Connection management. connect() verifies the device by reading back all
    // channel registers and seeds ChannelState from the hardware (a channel
    // reads as enabled when its duty is non-zero).
    bool connect(int comPort, int baudRate, uint8_t modbusAddress);
    void disconnect();
    bool isConnected() const;

    // Control. Channel is 0-based [0, CHANNEL_COUNT). Values are clamped to
    // the module's range before writing. setDutyCycle stores the configured
    // duty and writes it only while the channel is enabled; setOutputEnabled
    // gates the pulse train (duty 0 = off) without losing the configured duty.
    bool setFrequency(int channel, double hz);
    bool setDutyCycle(int channel, double percent);
    bool setOutputEnabled(int channel, bool on);

    Status getStatus() const;
    Config getConfig() const;

    // --- Pure encoding helpers (unit-testable without a serial port) -------
    static double clampFrequency(double hz);
    static double clampDuty(double percent);
    static uint32_t frequencyToRegisterValue(double hz);  // round(Hz * 100)
    static uint16_t dutyToRegisterValue(double percent);  // round(% * 100)
    // FC16 write of [freq high word, freq low word] at register 3*channel.
    static QByteArray buildFrequencyFrame(uint8_t addr, int channel, double hz);
    // FC06 write of the duty register 3*channel + 2.
    static QByteArray buildDutyFrame(uint8_t addr, int channel, double percent);

private:
    bool sendRequest(const QByteArray& request, QByteArray& response, int expectedBytes);
    bool readHoldingRegisters(uint16_t startReg, uint16_t count, QByteArray& data);
    bool writeFrame(const QByteArray& request);
    static bool validChannel(int channel);

    QSerialPort* serial_{nullptr};
    Config config_;
    Status status_;
    mutable std::mutex mutex_;
};

} // namespace backend::services
