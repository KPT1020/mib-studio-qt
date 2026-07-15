#include "backend/services/SyringePumpService.h"
#include "backend/services/ISerialPort.h"
#include "backend/services/ModbusRtu.h"

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

    // Space-separated hex dump for debug logging (replaces QByteArray::toHex).
    std::string toHexString(const std::vector<uint8_t>& bytes) {
        static const char* kHex = "0123456789abcdef";
        std::string out;
        out.reserve(bytes.size() * 3);
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (i) out.push_back(' ');
            out.push_back(kHex[bytes[i] >> 4]);
            out.push_back(kHex[bytes[i] & 0x0F]);
        }
        return out;
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
std::vector<uint8_t> SyringePumpService::floatToRegisters(float value) {
    return modbus::floatToRegisters(value);
}

float SyringePumpService::registersToFloat(const uint8_t* data) {
    return modbus::registersToFloat(data);
}

// ---------------------------------------------------------------------------
// Modbus RTU frame builders
// ---------------------------------------------------------------------------
std::vector<uint8_t> SyringePumpService::buildReadRequest(uint8_t addr, uint16_t startReg, uint16_t count) {
    return modbus::buildReadRequest(addr, startReg, count);
}

std::vector<uint8_t> SyringePumpService::buildWriteSingleRequest(uint8_t addr, uint16_t reg, uint16_t value) {
    return modbus::buildWriteSingleRequest(addr, reg, value);
}

std::vector<uint8_t> SyringePumpService::buildWriteMultipleRequest(uint8_t addr, uint16_t startReg, const std::vector<uint8_t>& regData) {
    return modbus::buildWriteMultipleRequest(addr, startReg, regData);
}

// ---------------------------------------------------------------------------
// Serial send/receive
// ---------------------------------------------------------------------------
bool SyringePumpService::sendRequest(int pumpIdx, const std::vector<uint8_t>& request, std::vector<uint8_t>& response, int expectedBytes) {
    auto& pump = pumps_[static_cast<size_t>(pumpIdx)];
    if (!pump.serial || !pump.serial->isOpen()) {
        return false;
    }

    // Clear any pending data
    pump.serial->readAll();

    // Small inter-frame delay (Modbus RTU requires 3.5 char silence between frames)
    std::this_thread::sleep_for(std::chrono::milliseconds(INTER_FRAME_DELAY_MS));

    SPDLOG_DEBUG("SyringePumpService: TX pump {} [{}]: {}",
                pumpIdx, request.size(), toHexString(request));

    // Write request
    const int written = pump.serial->write(request);
    if (written != static_cast<int>(request.size())) {
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
                        pumpIdx, response.size(), expectedBytes, toHexString(response));
            return false;
        }
        const std::vector<uint8_t> chunk = pump.serial->readAll();
        response.insert(response.end(), chunk.begin(), chunk.end());

        // Detect Modbus exception early (always 5 bytes)
        if (response.size() >= 5 && (response[1] & 0x80)) {
            response.resize(5);
            break;
        }

        remaining = expectedBytes - static_cast<int>(response.size());
    }

    SPDLOG_DEBUG("SyringePumpService: RX pump {} [{}]: {}",
                pumpIdx, response.size(), toHexString(response));

    // Verify CRC (also rejects frames shorter than 4 bytes).
    if (!modbus::responseCrcValid(response)) {
        SPDLOG_ERROR("SyringePumpService: bad/short response ({} bytes) from pump {}: {}",
                    response.size(), pumpIdx, toHexString(response));
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
bool SyringePumpService::readHoldingRegisters(int pumpIdx, uint16_t startReg, uint16_t count, std::vector<uint8_t>& data) {
    auto& pump = pumps_[static_cast<size_t>(pumpIdx)];
    std::vector<uint8_t> request = buildReadRequest(pump.config.modbusAddress, startReg, count);
    // Expected response: addr(1) + func(1) + byteCount(1) + data(count*2) + crc(2)
    int expectedBytes = 3 + count * 2 + 2;
    std::vector<uint8_t> response;
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
    std::vector<uint8_t> request = buildWriteSingleRequest(pump.config.modbusAddress, reg, value);
    // Expected response: echo of request (8 bytes)
    std::vector<uint8_t> response;
    return sendRequest(pumpIdx, request, response, 8);
}

bool SyringePumpService::writeMultipleRegisters(int pumpIdx, uint16_t startReg, const std::vector<uint8_t>& regData) {
    auto& pump = pumps_[static_cast<size_t>(pumpIdx)];
    std::vector<uint8_t> request = buildWriteMultipleRequest(pump.config.modbusAddress, startReg, regData);
    // Expected response: addr(1) + func(1) + startReg(2) + count(2) + crc(2) = 8
    std::vector<uint8_t> response;
    return sendRequest(pumpIdx, request, response, 8);
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
SyringePumpService::SyringePumpService()
    : serialPortFactory_([] { return makePlatformSerialPort(); }) {}

SyringePumpService::~SyringePumpService() {
    disconnect(PumpId::Sample);
    disconnect(PumpId::Sheath);
}

void SyringePumpService::setSerialPortFactory(SerialPortFactory factory) {
    serialPortFactory_ = std::move(factory);
}

// Create a serial port via the injected factory (falls back to the platform
// port if a caller cleared the factory).
namespace {
    std::unique_ptr<ISerialPort> makeSerial(const SerialPortFactory& factory) {
        return factory ? factory() : makePlatformSerialPort();
    }
} // namespace

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

    // Create and open the serial port (8N1, no flow control — fixed by the impl).
    pump.serial = makeSerial(serialPortFactory_);
    if (!pump.serial || !pump.serial->open(comPort, baudRate)) {
        SPDLOG_ERROR("SyringePumpService: Failed to open COM{} for {} pump: {}",
                    comPort, pumpName(id),
                    pump.serial ? pump.serial->lastError() : std::string("no serial port"));
        pump.serial.reset();
        return false;
    }
    SPDLOG_INFO("SyringePumpService: COM{} opened for {} pump (baud={}, addr={})",
                comPort, pumpName(id), baudRate, modbusAddress);

    // Verify communication by enabling the channel (required for start/stop commands)
    if (!writeSingleRegister(idx, REG_CHANNEL_ENABLE, 1)) {
        SPDLOG_ERROR("SyringePumpService: {} pump not responding on COM{} addr={} — check wiring and address",
                     pumpName(id), comPort, modbusAddress);
        pump.serial->close();
        pump.serial.reset();
        return false;
    }

    // Read min/max flow rates
    std::vector<uint8_t> minData, maxData;
    if (readHoldingRegisters(idx, REG_MIN_FLOW_RATE, 2, minData)) {
        pump.status.minFlowRate = registersToFloat(minData.data());
        SPDLOG_INFO("SyringePumpService: {} pump min flow rate: {}", pumpName(id), pump.status.minFlowRate);
    }
    if (readHoldingRegisters(idx, REG_MAX_FLOW_RATE, 2, maxData)) {
        pump.status.maxFlowRate = registersToFloat(maxData.data());
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
        pump.serial.reset();
    }
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

    std::unique_ptr<ISerialPort> serial = makeSerial(serialPortFactory_);
    if (!serial || !serial->open(comPort, baudRate)) {
        SPDLOG_WARN("SyringePumpService: scan failed to open COM{}: {}",
                    comPort, serial ? serial->lastError() : std::string("no serial port"));
        return addresses;
    }

    for (uint16_t addr = startAddress; addr <= endAddress; ++addr) {
        // FC03 read of REG_RUN_COMMAND (1 register) — a device at `addr` echoes
        // its own address in a well-formed response.
        const std::vector<uint8_t> request =
            modbus::buildReadRequest(static_cast<uint8_t>(addr), REG_RUN_COMMAND, 1);

        serial->readAll();
        std::this_thread::sleep_for(std::chrono::milliseconds(INTER_FRAME_DELAY_MS));

        if (serial->write(request) != static_cast<int>(request.size())) {
            continue;
        }
        if (!serial->waitForBytesWritten(timeoutMs)) {
            continue;
        }
        if (!serial->waitForReadyRead(timeoutMs)) {
            continue;
        }

        std::vector<uint8_t> response = serial->readAll();
        while (serial->waitForReadyRead(20)) {
            const std::vector<uint8_t> more = serial->readAll();
            response.insert(response.end(), more.begin(), more.end());
            if (response.size() >= 7) {
                break;
            }
        }
        if (response.size() < 5) {
            continue;
        }

        const uint8_t rxAddr = response[0];
        const uint8_t rxFunc = response[1];
        if (rxAddr != static_cast<uint8_t>(addr)) {
            continue;
        }
        if (rxFunc & 0x80) {
            continue;
        }
        if (rxFunc != FUNC_READ_HOLDING) {
            continue;
        }

        const size_t frameSize = 3 + static_cast<size_t>(response[2]) + 2;
        if (response.size() < frameSize || frameSize < 5) {
            continue;
        }
        response.resize(frameSize);

        if (!modbus::responseCrcValid(response)) {
            continue;
        }

        addresses.push_back(static_cast<uint8_t>(addr));
    }

    serial->close();
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
    std::vector<uint8_t> runData;
    if (readHoldingRegisters(idx, REG_RUN_COMMAND, 1, runData)) {
        uint16_t running = static_cast<uint16_t>((runData[0] << 8) | runData[1]);
        if (running == 0) {
            pump.status.runStatus = RunStatus::Stop;
        } else {
            // Read direction to distinguish forward/backward
            std::vector<uint8_t> dirData;
            if (readHoldingRegisters(idx, REG_DIRECTION_STATUS, 1, dirData)) {
                uint16_t dir = static_cast<uint16_t>((dirData[0] << 8) | dirData[1]);
                pump.status.runStatus = (dir == 2) ? RunStatus::Backward : RunStatus::Forward;
            } else {
                pump.status.runStatus = RunStatus::Forward;
            }
        }
    }

    // Read error status (bit3 = stall/blockage)
    std::vector<uint8_t> errData;
    if (readHoldingRegisters(idx, REG_ERROR_STATUS, 1, errData)) {
        uint16_t err = static_cast<uint16_t>((errData[0] << 8) | errData[1]);
        pump.status.stalled = (err & 0x0008) != 0;
    }

    // Read current flow rate (uint16 from realtime register)
    std::vector<uint8_t> flowData;
    if (readHoldingRegisters(idx, REG_REALTIME_INFUSE_FLOW, 1, flowData)) {
        pump.status.currentFlowRate = static_cast<double>((flowData[0] << 8) | flowData[1]);
    }

    // Read accumulated volume
    std::vector<uint8_t> volData;
    if (readHoldingRegisters(idx, REG_ACCUM_VOLUME, 2, volData)) {
        pump.status.accumulatedVolume = registersToFloat(volData.data());
    }
}

} // namespace backend::services
