#include "backend/services/SyringePumpService.h"
#include "backend/services/ModbusRtu.h"

#include <QSerialPort>
#include <QByteArray>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>
#include <thread>
#include <chrono>

namespace backend::services {

namespace {
    // Modbus RTU register addresses (from dLSP 501X manual Appendix B)
    constexpr uint16_t REG_CHANNEL_ENABLE    = 0x0000;  // 0=disable, 1=enable (must be 1 to allow start)
    constexpr uint16_t REG_RUN_COMMAND       = 0x0001;  // 0=stop, 1=start
    constexpr uint16_t REG_FULL_SPEED_RUN    = 0x0008;  // 0=stop, 1=full speed infuse, 2=full speed withdraw
    constexpr uint16_t REG_ERROR_STATUS      = 0x0100;  // bit3=stall/blockage
    constexpr uint16_t REG_DIRECTION_STATUS  = 0x010A;  // R: 0=none, 1=infuse, 2=withdraw
    constexpr uint16_t REG_MIN_FLOW_RATE     = 0x004A;  // float32, 2 regs
    constexpr uint16_t REG_MAX_FLOW_RATE     = 0x004C;  // float32, 2 regs
    constexpr uint16_t REG_MODE              = 0x0060;  // 0=infuse, 1=withdraw, 2=infuse+withdraw, 3=withdraw+infuse
    constexpr uint16_t REG_SYRINGE_VOLUME    = 0x0061;  // uint16, 1~9999
    constexpr uint16_t REG_SYRINGE_VOL_UNIT  = 0x0062;  // uint16, volume unit code
    constexpr uint16_t REG_INFUSE_FLOW_RATE  = 0x006A;  // uint16, 1~9999
    constexpr uint16_t REG_INFUSE_FLOW_UNIT  = 0x006B;  // uint16, unit code
    constexpr uint16_t REG_WITHDRAW_FLOW_RATE = 0x006C; // uint16, 1~9999
    constexpr uint16_t REG_WITHDRAW_FLOW_UNIT = 0x006D; // uint16, unit code
    constexpr uint16_t REG_REALTIME_INFUSE_FLOW = 0x0102; // uint16, read-only
    constexpr uint16_t REG_ACCUM_VOLUME      = 0x00C7;  // float32, 2 regs

    // Modbus function codes
    constexpr uint8_t FUNC_READ_HOLDING      = 0x03;
    constexpr uint8_t FUNC_WRITE_SINGLE      = 0x06;
    constexpr uint8_t FUNC_WRITE_MULTIPLE    = 0x10;

    // Serial timeout in milliseconds
    constexpr int SERIAL_TIMEOUT_MS = 1000;

    // Inter-frame silence for Modbus RTU (3.5 char times: ~0.3ms at 115200, ~4ms at 9600)
    constexpr int INTER_FRAME_DELAY_MS = 5;

    const char* pumpName(SyringePumpService::PumpId id) {
        return id == SyringePumpService::PumpId::Sample ? "Sample" : "Sheath";
    }
} // namespace

// ---------------------------------------------------------------------------
// CRC16 — standard Modbus polynomial (0xA001 reflected)
// ---------------------------------------------------------------------------
uint16_t SyringePumpService::crc16(const uint8_t* data, size_t len) {
    return modbus::crc16(data, len);
}

// ---------------------------------------------------------------------------
// Float32 <-> Modbus register conversion (big-endian ABCD word order)
// ---------------------------------------------------------------------------
QByteArray SyringePumpService::floatToRegisters(float value) {
    return modbus::floatToRegisters(value);
}

float SyringePumpService::registersToFloat(const uint8_t* data) {
    return modbus::registersToFloat(data);
}

// ---------------------------------------------------------------------------
// Modbus RTU frame builders
// ---------------------------------------------------------------------------
QByteArray SyringePumpService::buildReadRequest(uint8_t addr, uint16_t startReg, uint16_t count) {
    return modbus::buildReadRequest(addr, startReg, count);
}

QByteArray SyringePumpService::buildWriteSingleRequest(uint8_t addr, uint16_t reg, uint16_t value) {
    return modbus::buildWriteSingleRequest(addr, reg, value);
}

QByteArray SyringePumpService::buildWriteMultipleRequest(uint8_t addr, uint16_t startReg, const QByteArray& regData) {
    return modbus::buildWriteMultipleRequest(addr, startReg, regData);
}

// ---------------------------------------------------------------------------
// Serial send/receive
// ---------------------------------------------------------------------------
bool SyringePumpService::sendRequest(int pumpIdx, const QByteArray& request, QByteArray& response, int expectedBytes) {
    auto& pump = pumps_[static_cast<size_t>(pumpIdx)];
    if (!pump.serial || !pump.serial->isOpen()) {
        return false;
    }

    // Clear any pending data
    pump.serial->readAll();

    // Small inter-frame delay (Modbus RTU requires 3.5 char silence between frames)
    std::this_thread::sleep_for(std::chrono::milliseconds(INTER_FRAME_DELAY_MS));

    SPDLOG_DEBUG("SyringePumpService: TX pump {} [{}]: {}",
                pumpIdx, request.size(), request.toHex(' ').constData());

    // Write request
    qint64 written = pump.serial->write(request);
    if (written != request.size()) {
        SPDLOG_ERROR("SyringePumpService: Failed to write {} bytes to pump {}", request.size(), pumpIdx);
        return false;
    }
    if (!pump.serial->waitForBytesWritten(SERIAL_TIMEOUT_MS)) {
        SPDLOG_ERROR("SyringePumpService: Write timeout for pump {}", pumpIdx);
        return false;
    }

    // Read response — exception frames are always 5 bytes (addr + func|0x80 + code + crc16)
    response.clear();
    int remaining = expectedBytes;
    while (remaining > 0) {
        if (!pump.serial->waitForReadyRead(SERIAL_TIMEOUT_MS)) {
            SPDLOG_ERROR("SyringePumpService: Read timeout for pump {} (got {}/{} bytes): {}",
                        pumpIdx, response.size(), expectedBytes, response.toHex(' ').constData());
            return false;
        }
        QByteArray chunk = pump.serial->readAll();
        response.append(chunk);

        // Detect Modbus exception early (always 5 bytes)
        if (response.size() >= 5 && (static_cast<uint8_t>(response[1]) & 0x80)) {
            response.truncate(5);
            break;
        }

        remaining = expectedBytes - response.size();
    }

    SPDLOG_DEBUG("SyringePumpService: RX pump {} [{}]: {}",
                pumpIdx, response.size(), response.toHex(' ').constData());

    // Verify CRC (also rejects frames shorter than 4 bytes).
    if (!modbus::responseCrcValid(response)) {
        SPDLOG_ERROR("SyringePumpService: bad/short response ({} bytes) from pump {}: {}",
                    response.size(), pumpIdx, response.toHex(' ').constData());
        return false;
    }

    // Check for Modbus exception response (CRC already validated -> >=4 bytes).
    if (modbus::isExceptionFrame(response)) {
        SPDLOG_ERROR("SyringePumpService: Modbus exception 0x{:02X} from pump {} (func=0x{:02X})",
                    static_cast<uint8_t>(response[2]), pumpIdx, static_cast<uint8_t>(response[1]) & 0x7F);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// High-level Modbus read/write
// ---------------------------------------------------------------------------
bool SyringePumpService::readHoldingRegisters(int pumpIdx, uint16_t startReg, uint16_t count, QByteArray& data) {
    auto& pump = pumps_[static_cast<size_t>(pumpIdx)];
    QByteArray request = buildReadRequest(pump.config.modbusAddress, startReg, count);
    // Expected response: addr(1) + func(1) + byteCount(1) + data(count*2) + crc(2)
    int expectedBytes = 3 + count * 2 + 2;
    QByteArray response;
    if (!sendRequest(pumpIdx, request, response, expectedBytes)) {
        return false;
    }
    // Extract data with bounds + byteCount validation so a short/garbled frame
    // can't yield fewer bytes than the caller indexes.
    if (!modbus::extractReadData(response, count, data)) {
        SPDLOG_ERROR("SyringePumpService: malformed read response from pump {} "
                     "(expected {} registers, got {} bytes)",
                     pumpIdx, count, response.size());
        return false;
    }
    return true;
}

bool SyringePumpService::writeSingleRegister(int pumpIdx, uint16_t reg, uint16_t value) {
    auto& pump = pumps_[static_cast<size_t>(pumpIdx)];
    QByteArray request = buildWriteSingleRequest(pump.config.modbusAddress, reg, value);
    // Expected response: echo of request (8 bytes)
    QByteArray response;
    return sendRequest(pumpIdx, request, response, 8);
}

bool SyringePumpService::writeMultipleRegisters(int pumpIdx, uint16_t startReg, const QByteArray& regData) {
    auto& pump = pumps_[static_cast<size_t>(pumpIdx)];
    QByteArray request = buildWriteMultipleRequest(pump.config.modbusAddress, startReg, regData);
    // Expected response: addr(1) + func(1) + startReg(2) + count(2) + crc(2) = 8
    QByteArray response;
    return sendRequest(pumpIdx, request, response, 8);
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
SyringePumpService::SyringePumpService() = default;

SyringePumpService::~SyringePumpService() {
    disconnect(PumpId::Sample);
    disconnect(PumpId::Sheath);
}

// ---------------------------------------------------------------------------
// Connection management
// ---------------------------------------------------------------------------
bool SyringePumpService::connect(PumpId id, int comPort, int baudRate, uint8_t modbusAddress) {
    int idx = static_cast<int>(id);
    auto& pump = pumps_[static_cast<size_t>(idx)];

    {
        std::scoped_lock lock(pump.mutex);
        if (pump.serial && pump.serial->isOpen()) {
            disconnect(id);
        }
    }

    std::scoped_lock lock(pump.mutex);

    pump.config.comPort = comPort;
    pump.config.baudRate = baudRate;
    pump.config.modbusAddress = modbusAddress;

    // Create and configure serial port
    pump.serial = new QSerialPort();
    pump.serial->setPortName(QString("COM%1").arg(comPort));
    pump.serial->setBaudRate(baudRate);
    pump.serial->setDataBits(QSerialPort::Data8);
    pump.serial->setParity(QSerialPort::NoParity);
    pump.serial->setStopBits(QSerialPort::OneStop);
    pump.serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!pump.serial->open(QIODevice::ReadWrite)) {
        SPDLOG_ERROR("SyringePumpService: Failed to open COM{} for {} pump: {}",
                    comPort, pumpName(id), pump.serial->errorString().toStdString());
        delete pump.serial;
        pump.serial = nullptr;
        return false;
    }
    SPDLOG_INFO("SyringePumpService: COM{} opened for {} pump (baud={}, addr={})",
                comPort, pumpName(id), baudRate, modbusAddress);

    // Verify communication by enabling the channel (required for start/stop commands)
    if (!writeSingleRegister(idx, REG_CHANNEL_ENABLE, 1)) {
        SPDLOG_ERROR("SyringePumpService: {} pump not responding on COM{} addr={} — check wiring and address",
                     pumpName(id), comPort, modbusAddress);
        pump.serial->close();
        delete pump.serial;
        pump.serial = nullptr;
        return false;
    }

    // Read min/max flow rates
    QByteArray minData, maxData;
    if (readHoldingRegisters(idx, REG_MIN_FLOW_RATE, 2, minData)) {
        pump.status.minFlowRate = registersToFloat(reinterpret_cast<const uint8_t*>(minData.constData()));
        SPDLOG_INFO("SyringePumpService: {} pump min flow rate: {}", pumpName(id), pump.status.minFlowRate);
    }
    if (readHoldingRegisters(idx, REG_MAX_FLOW_RATE, 2, maxData)) {
        pump.status.maxFlowRate = registersToFloat(reinterpret_cast<const uint8_t*>(maxData.constData()));
        SPDLOG_INFO("SyringePumpService: {} pump max flow rate: {}", pumpName(id), pump.status.maxFlowRate);
    }

    pump.status.connected = true;
    SPDLOG_INFO("SyringePumpService: {} pump connected on COM{}", pumpName(id), comPort);
    return true;
}

void SyringePumpService::disconnect(PumpId id) {
    int idx = static_cast<int>(id);
    auto& pump = pumps_[static_cast<size_t>(idx)];

    std::scoped_lock lock(pump.mutex);
    if (!pump.serial) {
        return;
    }

    // Try to stop the pump before disconnecting
    if (pump.status.connected && pump.status.runStatus != RunStatus::Stop) {
        writeSingleRegister(idx, REG_RUN_COMMAND, 0);
    }

    if (pump.serial) {
        if (pump.serial->isOpen()) {
            pump.serial->close();
        }
        delete pump.serial;
    }
    pump.serial = nullptr;
    pump.status.connected = false;
    pump.status.runStatus = RunStatus::Stop;
    pump.status.currentFlowRate = 0.0;
    pump.status.accumulatedVolume = 0.0;

    SPDLOG_INFO("SyringePumpService: {} pump disconnected", pumpName(id));
}

bool SyringePumpService::isConnected(PumpId id) const {
    int idx = static_cast<int>(id);
    const auto& pump = pumps_[static_cast<size_t>(idx)];
    std::scoped_lock lock(pump.mutex);
    return pump.status.connected;
}

// ---------------------------------------------------------------------------
// Control methods
// ---------------------------------------------------------------------------
bool SyringePumpService::setFlowRate(PumpId id, double rate, uint16_t unit) {
    int idx = static_cast<int>(id);
    auto& pump = pumps_[static_cast<size_t>(idx)];
    std::scoped_lock lock(pump.mutex);

    if (!pump.status.connected) return false;

    uint16_t rateValue = static_cast<uint16_t>(std::clamp(rate, 1.0, 9999.0));

    // Set both infuse and withdraw flow rates
    if (!writeSingleRegister(idx, REG_INFUSE_FLOW_RATE, rateValue)) {
        SPDLOG_ERROR("SyringePumpService: Failed to set infuse flow rate for {} pump", pumpName(id));
        return false;
    }
    if (!writeSingleRegister(idx, REG_INFUSE_FLOW_UNIT, unit)) {
        SPDLOG_ERROR("SyringePumpService: Failed to set infuse flow rate unit for {} pump", pumpName(id));
        return false;
    }
    if (!writeSingleRegister(idx, REG_WITHDRAW_FLOW_RATE, rateValue)) {
        SPDLOG_ERROR("SyringePumpService: Failed to set withdraw flow rate for {} pump", pumpName(id));
        return false;
    }
    if (!writeSingleRegister(idx, REG_WITHDRAW_FLOW_UNIT, unit)) {
        SPDLOG_ERROR("SyringePumpService: Failed to set withdraw flow rate unit for {} pump", pumpName(id));
        return false;
    }

    pump.config.flowRate = rate;
    pump.config.flowRateUnit = unit;
    SPDLOG_INFO("SyringePumpService: {} pump flow rate set to {} (unit={})", pumpName(id), rateValue, unit);
    return true;
}

bool SyringePumpService::setDirection(PumpId id, Direction dir) {
    int idx = static_cast<int>(id);
    auto& pump = pumps_[static_cast<size_t>(idx)];
    std::scoped_lock lock(pump.mutex);

    if (!pump.status.connected) return false;

    if (!writeSingleRegister(idx, REG_MODE, static_cast<uint16_t>(dir))) {
        SPDLOG_ERROR("SyringePumpService: Failed to set direction for {} pump", pumpName(id));
        return false;
    }

    pump.config.direction = dir;
    SPDLOG_INFO("SyringePumpService: {} pump direction set to {}",
                pumpName(id), dir == Direction::Infuse ? "Infuse" : "Withdraw");
    return true;
}

bool SyringePumpService::start(PumpId id) {
    int idx = static_cast<int>(id);
    auto& pump = pumps_[static_cast<size_t>(idx)];
    std::scoped_lock lock(pump.mutex);

    if (!pump.status.connected) return false;

    // Ensure channel is enabled before starting
    if (!writeSingleRegister(idx, REG_CHANNEL_ENABLE, 1)) {
        SPDLOG_WARN("SyringePumpService: Could not enable channel for {} pump", pumpName(id));
    }

    if (!writeSingleRegister(idx, REG_RUN_COMMAND, 1)) {
        SPDLOG_ERROR("SyringePumpService: Failed to start {} pump", pumpName(id));
        return false;
    }

    SPDLOG_INFO("SyringePumpService: {} pump started", pumpName(id));
    return true;
}

bool SyringePumpService::stop(PumpId id) {
    int idx = static_cast<int>(id);
    auto& pump = pumps_[static_cast<size_t>(idx)];
    std::scoped_lock lock(pump.mutex);

    if (!pump.status.connected) return false;

    if (!writeSingleRegister(idx, REG_RUN_COMMAND, 0)) {
        SPDLOG_ERROR("SyringePumpService: Failed to stop {} pump", pumpName(id));
        return false;
    }

    SPDLOG_INFO("SyringePumpService: {} pump stopped", pumpName(id));
    return true;
}

bool SyringePumpService::purge(PumpId id, Direction dir) {
    int idx = static_cast<int>(id);
    auto& pump = pumps_[static_cast<size_t>(idx)];
    std::scoped_lock lock(pump.mutex);

    if (!pump.status.connected) return false;

    if (!writeSingleRegister(idx, REG_CHANNEL_ENABLE, 1)) {
        SPDLOG_WARN("SyringePumpService: Could not enable channel for {} pump", pumpName(id));
    }

    // Full speed: 1=infuse, 2=withdraw
    uint16_t value = (dir == Direction::Infuse) ? 1 : 2;
    if (!writeSingleRegister(idx, REG_FULL_SPEED_RUN, value)) {
        SPDLOG_ERROR("SyringePumpService: Failed to purge {} pump", pumpName(id));
        return false;
    }

    SPDLOG_INFO("SyringePumpService: {} pump purge started ({})",
                pumpName(id), dir == Direction::Infuse ? "infuse" : "withdraw");
    return true;
}

bool SyringePumpService::stopPurge(PumpId id) {
    int idx = static_cast<int>(id);
    auto& pump = pumps_[static_cast<size_t>(idx)];
    std::scoped_lock lock(pump.mutex);

    if (!pump.status.connected) return false;

    if (!writeSingleRegister(idx, REG_FULL_SPEED_RUN, 0)) {
        SPDLOG_ERROR("SyringePumpService: Failed to stop purge for {} pump", pumpName(id));
        return false;
    }

    SPDLOG_INFO("SyringePumpService: {} pump purge stopped", pumpName(id));
    return true;
}

bool SyringePumpService::setSyringeVolume(PumpId id, uint16_t volume, uint16_t unit) {
    int idx = static_cast<int>(id);
    auto& pump = pumps_[static_cast<size_t>(idx)];
    std::scoped_lock lock(pump.mutex);

    if (!pump.status.connected) return false;

    uint16_t clampedVol = static_cast<uint16_t>(std::clamp(static_cast<int>(volume), 1, 9999));
    if (!writeSingleRegister(idx, REG_SYRINGE_VOLUME, clampedVol)) {
        SPDLOG_ERROR("SyringePumpService: Failed to set syringe volume for {} pump", pumpName(id));
        return false;
    }
    if (!writeSingleRegister(idx, REG_SYRINGE_VOL_UNIT, unit)) {
        SPDLOG_ERROR("SyringePumpService: Failed to set syringe volume unit for {} pump", pumpName(id));
        return false;
    }

    SPDLOG_INFO("SyringePumpService: {} pump syringe volume set to {} (unit={})", pumpName(id), clampedVol, unit);
    return true;
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
// REMOVED: setModbusAddress, setModbusAddressOneShot, scanModbusAddress
// (address is volatile on dLSP 501X; using separate COM ports instead)

SyringePumpService::PumpStatus SyringePumpService::getStatus(PumpId id) const {
    int idx = static_cast<int>(id);
    auto& pump = pumps_[static_cast<size_t>(idx)];
    std::scoped_lock lock(pump.mutex);

    return pump.status;
}

SyringePumpService::PumpConfig SyringePumpService::getConfig(PumpId id) const {
    int idx = static_cast<int>(id);
    const auto& pump = pumps_[static_cast<size_t>(idx)];
    std::scoped_lock lock(pump.mutex);
    return pump.config;
}

void SyringePumpService::setConfig(PumpId id, const PumpConfig& config) {
    int idx = static_cast<int>(id);
    auto& pump = pumps_[static_cast<size_t>(idx)];
    std::scoped_lock lock(pump.mutex);
    pump.config = config;
}

int SyringePumpService::getComPort(PumpId id) const {
    int idx = static_cast<int>(id);
    const auto& pump = pumps_[static_cast<size_t>(idx)];
    std::scoped_lock lock(pump.mutex);
    return pump.config.comPort;
}

std::vector<uint8_t> SyringePumpService::scanModbusAddresses(
    int comPort,
    int baudRate,
    uint8_t startAddress,
    uint8_t endAddress,
    int timeoutMs) {
    std::vector<uint8_t> addresses;
    if (comPort <= 0 || baudRate <= 0) {
        return addresses;
    }
    if (startAddress == 0 || endAddress == 0 || startAddress > endAddress) {
        return addresses;
    }

    QSerialPort serial;
    serial.setPortName(QString("COM%1").arg(comPort));
    serial.setBaudRate(baudRate);
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setStopBits(QSerialPort::OneStop);
    serial.setFlowControl(QSerialPort::NoFlowControl);

    if (!serial.open(QIODevice::ReadWrite)) {
        SPDLOG_WARN("SyringePumpService: scan failed to open COM{}: {}",
                    comPort, serial.errorString().toStdString());
        return addresses;
    }

    for (uint16_t addr = startAddress; addr <= endAddress; ++addr) {
        QByteArray request(6, 0);
        request[0] = static_cast<char>(addr);
        request[1] = static_cast<char>(FUNC_READ_HOLDING);
        request[2] = static_cast<char>((REG_RUN_COMMAND >> 8) & 0xFF);
        request[3] = static_cast<char>(REG_RUN_COMMAND & 0xFF);
        request[4] = 0x00;
        request[5] = 0x01;
        uint16_t crc = crc16(reinterpret_cast<const uint8_t*>(request.constData()), 6);
        request.append(static_cast<char>(crc & 0xFF));
        request.append(static_cast<char>((crc >> 8) & 0xFF));

        serial.readAll();
        std::this_thread::sleep_for(std::chrono::milliseconds(INTER_FRAME_DELAY_MS));

        if (serial.write(request) != request.size()) {
            continue;
        }
        if (!serial.waitForBytesWritten(timeoutMs)) {
            continue;
        }
        if (!serial.waitForReadyRead(timeoutMs)) {
            continue;
        }

        QByteArray response = serial.readAll();
        while (serial.waitForReadyRead(20)) {
            response.append(serial.readAll());
            if (response.size() >= 7) {
                break;
            }
        }
        if (response.size() < 5) {
            continue;
        }

        const uint8_t rxAddr = static_cast<uint8_t>(response[0]);
        const uint8_t rxFunc = static_cast<uint8_t>(response[1]);
        if (rxAddr != static_cast<uint8_t>(addr)) {
            continue;
        }
        if (rxFunc & 0x80) {
            continue;
        }
        if (rxFunc != FUNC_READ_HOLDING) {
            continue;
        }

        const int frameSize = 3 + static_cast<int>(static_cast<uint8_t>(response[2])) + 2;
        if (response.size() < frameSize || frameSize < 5) {
            continue;
        }
        response = response.left(frameSize);

        const size_t dataLen = static_cast<size_t>(response.size()) - 2;
        const uint16_t receivedCrc = static_cast<uint16_t>(
            (static_cast<uint8_t>(response[response.size() - 1]) << 8) |
             static_cast<uint8_t>(response[response.size() - 2]));
        const uint16_t calculatedCrc = crc16(reinterpret_cast<const uint8_t*>(response.constData()), dataLen);
        if (receivedCrc != calculatedCrc) {
            continue;
        }

        addresses.push_back(static_cast<uint8_t>(addr));
    }

    serial.close();
    return addresses;
}

void SyringePumpService::pollStatus(PumpId id) {
    int idx = static_cast<int>(id);
    auto& pump = pumps_[static_cast<size_t>(idx)];
    std::scoped_lock lock(pump.mutex);

    if (!pump.status.connected || !pump.serial || !pump.serial->isOpen()) {
        return;
    }

    // Read run state (0=stopped, 1=running)
    QByteArray runData;
    if (readHoldingRegisters(idx, REG_RUN_COMMAND, 1, runData)) {
        uint16_t running = static_cast<uint16_t>(
            (static_cast<uint8_t>(runData[0]) << 8) | static_cast<uint8_t>(runData[1]));
        if (running == 0) {
            pump.status.runStatus = RunStatus::Stop;
        } else {
            // Read direction to distinguish forward/backward
            QByteArray dirData;
            if (readHoldingRegisters(idx, REG_DIRECTION_STATUS, 1, dirData)) {
                uint16_t dir = static_cast<uint16_t>(
                    (static_cast<uint8_t>(dirData[0]) << 8) | static_cast<uint8_t>(dirData[1]));
                pump.status.runStatus = (dir == 2) ? RunStatus::Backward : RunStatus::Forward;
            } else {
                pump.status.runStatus = RunStatus::Forward;
            }
        }
    }

    // Read error status (bit3 = stall/blockage)
    QByteArray errData;
    if (readHoldingRegisters(idx, REG_ERROR_STATUS, 1, errData)) {
        uint16_t err = static_cast<uint16_t>(
            (static_cast<uint8_t>(errData[0]) << 8) | static_cast<uint8_t>(errData[1]));
        pump.status.stalled = (err & 0x0008) != 0;
    }

    // Read current flow rate (uint16 from realtime register)
    QByteArray flowData;
    if (readHoldingRegisters(idx, REG_REALTIME_INFUSE_FLOW, 1, flowData)) {
        pump.status.currentFlowRate = static_cast<double>(
            (static_cast<uint8_t>(flowData[0]) << 8) | static_cast<uint8_t>(flowData[1]));
    }

    // Read accumulated volume
    QByteArray volData;
    if (readHoldingRegisters(idx, REG_ACCUM_VOLUME, 2, volData)) {
        pump.status.accumulatedVolume = registersToFloat(
            reinterpret_cast<const uint8_t*>(volData.constData()));
    }
}

} // namespace backend::services
