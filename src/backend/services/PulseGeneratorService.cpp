#include "backend/services/PulseGeneratorService.h"
#include "backend/services/ModbusRtu.h"

#include <QSerialPort>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace backend::services {

namespace {
    // Per-channel holding registers (protocol addresses, channel 0-based):
    // 3N = frequency high word, 3N+1 = frequency low word, 3N+2 = duty.
    constexpr uint16_t regFreq(int channel) { return static_cast<uint16_t>(channel * 3); }
    constexpr uint16_t regDuty(int channel) { return static_cast<uint16_t>(channel * 3 + 2); }

    constexpr int SERIAL_TIMEOUT_MS = 1000;
    // Modbus RTU inter-frame silence (3.5 char times: ~4ms at the module's
    // default 9600 baud).
    constexpr int INTER_FRAME_DELAY_MS = 5;
} // namespace

// ---------------------------------------------------------------------------
// Pure encoding helpers
// ---------------------------------------------------------------------------
double PulseGeneratorService::clampFrequency(double hz) {
    return std::clamp(hz, MIN_FREQUENCY_HZ, MAX_FREQUENCY_HZ);
}

double PulseGeneratorService::clampDuty(double percent) {
    return std::clamp(percent, 0.0, 100.0);
}

uint32_t PulseGeneratorService::frequencyToRegisterValue(double hz) {
    return static_cast<uint32_t>(std::llround(clampFrequency(hz) * 100.0));
}

uint16_t PulseGeneratorService::dutyToRegisterValue(double percent) {
    return static_cast<uint16_t>(std::lround(clampDuty(percent) * 100.0));
}

QByteArray PulseGeneratorService::buildFrequencyFrame(uint8_t addr, int channel, double hz) {
    const uint32_t value = frequencyToRegisterValue(hz);
    QByteArray regData(4, 0);
    regData[0] = static_cast<char>((value >> 24) & 0xFF); // high word, high byte
    regData[1] = static_cast<char>((value >> 16) & 0xFF); // high word, low byte
    regData[2] = static_cast<char>((value >> 8) & 0xFF);  // low word, high byte
    regData[3] = static_cast<char>(value & 0xFF);         // low word, low byte
    return modbus::buildWriteMultipleRequest(addr, regFreq(channel), regData);
}

QByteArray PulseGeneratorService::buildDutyFrame(uint8_t addr, int channel, double percent) {
    return modbus::buildWriteSingleRequest(addr, regDuty(channel), dutyToRegisterValue(percent));
}

bool PulseGeneratorService::validChannel(int channel) {
    return channel >= 0 && channel < CHANNEL_COUNT;
}

// ---------------------------------------------------------------------------
// Serial send/receive (same blocking pattern as SyringePumpService)
// ---------------------------------------------------------------------------
bool PulseGeneratorService::sendRequest(const QByteArray& request, QByteArray& response, int expectedBytes) {
    if (!serial_ || !serial_->isOpen()) {
        return false;
    }

    serial_->readAll(); // drop any stale bytes
    std::this_thread::sleep_for(std::chrono::milliseconds(INTER_FRAME_DELAY_MS));

    SPDLOG_DEBUG("PulseGeneratorService: TX [{}]: {}",
                 request.size(), request.toHex(' ').constData());

    if (serial_->write(request) != request.size()) {
        SPDLOG_ERROR("PulseGeneratorService: failed to write {} bytes", request.size());
        return false;
    }
    if (!serial_->waitForBytesWritten(SERIAL_TIMEOUT_MS)) {
        SPDLOG_ERROR("PulseGeneratorService: write timeout");
        return false;
    }

    response.clear();
    int remaining = expectedBytes;
    while (remaining > 0) {
        if (!serial_->waitForReadyRead(SERIAL_TIMEOUT_MS)) {
            SPDLOG_ERROR("PulseGeneratorService: read timeout (got {}/{} bytes): {}",
                         response.size(), expectedBytes, response.toHex(' ').constData());
            return false;
        }
        response.append(serial_->readAll());

        // Modbus exception frames are always 5 bytes (addr + func|0x80 + code + crc).
        if (response.size() >= 5 && (static_cast<uint8_t>(response[1]) & 0x80)) {
            response.truncate(5);
            break;
        }
        remaining = expectedBytes - response.size();
    }

    SPDLOG_DEBUG("PulseGeneratorService: RX [{}]: {}",
                 response.size(), response.toHex(' ').constData());

    if (!modbus::responseCrcValid(response)) {
        SPDLOG_ERROR("PulseGeneratorService: bad/short response ({} bytes): {}",
                     response.size(), response.toHex(' ').constData());
        return false;
    }
    if (modbus::isExceptionFrame(response)) {
        SPDLOG_ERROR("PulseGeneratorService: Modbus exception 0x{:02X} (func=0x{:02X})",
                     static_cast<uint8_t>(response[2]),
                     static_cast<uint8_t>(response[1]) & 0x7F);
        return false;
    }
    return true;
}

bool PulseGeneratorService::readHoldingRegisters(uint16_t startReg, uint16_t count, QByteArray& data) {
    const QByteArray request = modbus::buildReadRequest(config_.modbusAddress, startReg, count);
    const int expectedBytes = 3 + count * 2 + 2;
    QByteArray response;
    if (!sendRequest(request, response, expectedBytes)) {
        return false;
    }
    if (!modbus::extractReadData(response, count, data)) {
        SPDLOG_ERROR("PulseGeneratorService: malformed read response "
                     "(expected {} registers, got {} bytes)", count, response.size());
        return false;
    }
    return true;
}

bool PulseGeneratorService::writeFrame(const QByteArray& request) {
    // FC06 and FC16 both answer with an 8-byte confirmation frame.
    QByteArray response;
    return sendRequest(request, response, 8);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
PulseGeneratorService::PulseGeneratorService() = default;

PulseGeneratorService::~PulseGeneratorService() {
    disconnect();
}

bool PulseGeneratorService::connect(int comPort, int baudRate, uint8_t modbusAddress) {
    std::scoped_lock lock(mutex_);

    if (serial_) {
        if (serial_->isOpen()) {
            serial_->close();
        }
        delete serial_;
        serial_ = nullptr;
        status_ = Status{};
    }

    config_.comPort = comPort;
    config_.baudRate = baudRate;
    config_.modbusAddress = modbusAddress;

    serial_ = new QSerialPort();
    serial_->setPortName(QString("COM%1").arg(comPort));
    serial_->setBaudRate(baudRate);
    serial_->setDataBits(QSerialPort::Data8);
    serial_->setParity(QSerialPort::NoParity);
    serial_->setStopBits(QSerialPort::OneStop);
    serial_->setFlowControl(QSerialPort::NoFlowControl);

    if (!serial_->open(QIODevice::ReadWrite)) {
        SPDLOG_ERROR("PulseGeneratorService: failed to open COM{}: {}",
                     comPort, serial_->errorString().toStdString());
        delete serial_;
        serial_ = nullptr;
        return false;
    }
    SPDLOG_INFO("PulseGeneratorService: COM{} opened (baud={}, addr={})",
                comPort, baudRate, modbusAddress);

    // Verify the device and seed channel state from the hardware: all four
    // channels' freq/duty registers in one read. Does NOT write anything, so
    // a generator that is already pulsing keeps pulsing.
    QByteArray data;
    if (!readHoldingRegisters(0, CHANNEL_COUNT * 3, data)) {
        SPDLOG_ERROR("PulseGeneratorService: device not responding on COM{} addr={} "
                     "— check wiring and address", comPort, modbusAddress);
        serial_->close();
        delete serial_;
        serial_ = nullptr;
        return false;
    }
    const auto* regs = reinterpret_cast<const uint8_t*>(data.constData());
    for (int ch = 0; ch < CHANNEL_COUNT; ++ch) {
        const int off = ch * 6;
        const uint32_t freqRaw = (static_cast<uint32_t>(regs[off]) << 24) |
                                 (static_cast<uint32_t>(regs[off + 1]) << 16) |
                                 (static_cast<uint32_t>(regs[off + 2]) << 8) |
                                 static_cast<uint32_t>(regs[off + 3]);
        const uint16_t dutyRaw = static_cast<uint16_t>(
            (static_cast<uint16_t>(regs[off + 4]) << 8) | regs[off + 5]);
        auto& state = status_.channels[static_cast<size_t>(ch)];
        state.frequencyHz = freqRaw / 100.0;
        state.dutyPercent = dutyRaw / 100.0;
        state.outputEnabled = dutyRaw != 0;
    }

    status_.connected = true;
    SPDLOG_INFO("PulseGeneratorService: connected on COM{} (ch1: {} Hz, {} %)",
                comPort, status_.channels[0].frequencyHz, status_.channels[0].dutyPercent);
    return true;
}

void PulseGeneratorService::disconnect() {
    std::scoped_lock lock(mutex_);
    if (!serial_) {
        return;
    }
    // Deliberately leaves the module's outputs untouched: disconnecting the
    // control link must not stop a pulse train mid-experiment.
    if (serial_->isOpen()) {
        serial_->close();
    }
    delete serial_;
    serial_ = nullptr;
    status_.connected = false;
    SPDLOG_INFO("PulseGeneratorService: disconnected");
}

bool PulseGeneratorService::isConnected() const {
    std::scoped_lock lock(mutex_);
    return status_.connected;
}

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------
bool PulseGeneratorService::setFrequency(int channel, double hz) {
    if (!validChannel(channel)) {
        SPDLOG_ERROR("PulseGeneratorService: invalid channel {}", channel);
        return false;
    }
    std::scoped_lock lock(mutex_);
    if (!status_.connected) {
        return false;
    }
    const double clamped = clampFrequency(hz);
    if (clamped != hz) {
        SPDLOG_WARN("PulseGeneratorService: frequency {} Hz clamped to {} Hz", hz, clamped);
    }
    if (!writeFrame(buildFrequencyFrame(config_.modbusAddress, channel, clamped))) {
        return false;
    }
    status_.channels[static_cast<size_t>(channel)].frequencyHz = clamped;
    SPDLOG_INFO("PulseGeneratorService: ch{} frequency set to {} Hz", channel + 1, clamped);
    return true;
}

bool PulseGeneratorService::setDutyCycle(int channel, double percent) {
    if (!validChannel(channel)) {
        SPDLOG_ERROR("PulseGeneratorService: invalid channel {}", channel);
        return false;
    }
    std::scoped_lock lock(mutex_);
    if (!status_.connected) {
        return false;
    }
    const double clamped = clampDuty(percent);
    if (clamped != percent) {
        SPDLOG_WARN("PulseGeneratorService: duty {} % clamped to {} %", percent, clamped);
    }
    auto& state = status_.channels[static_cast<size_t>(channel)];
    // Only touch the device while the output is enabled; a disabled channel
    // keeps duty 0 on the wire and picks the new duty up on the next enable.
    if (state.outputEnabled &&
        !writeFrame(buildDutyFrame(config_.modbusAddress, channel, clamped))) {
        return false;
    }
    state.dutyPercent = clamped;
    SPDLOG_INFO("PulseGeneratorService: ch{} duty set to {} %{}", channel + 1, clamped,
                state.outputEnabled ? "" : " (deferred until enable)");
    return true;
}

bool PulseGeneratorService::setOutputEnabled(int channel, bool on) {
    if (!validChannel(channel)) {
        SPDLOG_ERROR("PulseGeneratorService: invalid channel {}", channel);
        return false;
    }
    std::scoped_lock lock(mutex_);
    if (!status_.connected) {
        return false;
    }
    auto& state = status_.channels[static_cast<size_t>(channel)];
    const double dutyToWrite = on ? state.dutyPercent : 0.0;
    if (!writeFrame(buildDutyFrame(config_.modbusAddress, channel, dutyToWrite))) {
        return false;
    }
    state.outputEnabled = on;
    SPDLOG_INFO("PulseGeneratorService: ch{} output {} (duty {} %)",
                channel + 1, on ? "enabled" : "disabled", dutyToWrite);
    return true;
}

PulseGeneratorService::Status PulseGeneratorService::getStatus() const {
    std::scoped_lock lock(mutex_);
    return status_;
}

PulseGeneratorService::Config PulseGeneratorService::getConfig() const {
    std::scoped_lock lock(mutex_);
    return config_;
}

} // namespace backend::services
