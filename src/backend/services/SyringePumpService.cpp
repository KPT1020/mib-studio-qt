#include "backend/services/SyringePumpService.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#error SyringePumpService Modbus serial is implemented for Windows only
#endif

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <thread>
#include <chrono>

namespace backend::services {

namespace {

constexpr uint16_t REG_CHANNEL_ENABLE    = 0x0000;
constexpr uint16_t REG_RUN_COMMAND       = 0x0001;
constexpr uint16_t REG_FULL_SPEED_RUN    = 0x0008;
constexpr uint16_t REG_ERROR_STATUS      = 0x0100;
constexpr uint16_t REG_DIRECTION_STATUS  = 0x010A;
constexpr uint16_t REG_MIN_FLOW_RATE     = 0x004A;
constexpr uint16_t REG_MAX_FLOW_RATE     = 0x004C;
constexpr uint16_t REG_MODE              = 0x0060;
constexpr uint16_t REG_SYRINGE_VOLUME    = 0x0061;
constexpr uint16_t REG_SYRINGE_VOL_UNIT  = 0x0062;
constexpr uint16_t REG_INFUSE_FLOW_RATE  = 0x006A;
constexpr uint16_t REG_INFUSE_FLOW_UNIT  = 0x006B;
constexpr uint16_t REG_WITHDRAW_FLOW_RATE = 0x006C;
constexpr uint16_t REG_WITHDRAW_FLOW_UNIT = 0x006D;
constexpr uint16_t REG_REALTIME_INFUSE_FLOW = 0x0102;
constexpr uint16_t REG_ACCUM_VOLUME      = 0x00C7;

constexpr uint8_t FUNC_READ_HOLDING      = 0x03;
constexpr uint8_t FUNC_WRITE_SINGLE      = 0x06;
constexpr uint8_t FUNC_WRITE_MULTIPLE    = 0x10;

constexpr int SERIAL_TIMEOUT_MS = 1000;
constexpr int INTER_FRAME_DELAY_MS = 5;

const char* pumpName(SyringePumpService::PumpId id) {
    return id == SyringePumpService::PumpId::Sample ? "Sample" : "Sheath";
}

std::string comPortName(int comPort) {
    if (comPort >= 10) {
        return "\\\\.\\COM" + std::to_string(comPort);
    }
    return "COM" + std::to_string(comPort);
}

std::string hexBytes(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    const char* hex = "0123456789abcdef";
    for (size_t i = 0; i < data.size(); ++i) {
        if (i) oss << ' ';
        oss << hex[(data[i] >> 4) & 0xf] << hex[data[i] & 0xf];
    }
    return oss.str();
}

#ifdef _WIN32
bool configureSerialHandle(HANDLE h, int baudRate) {
    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(h, &dcb)) {
        return false;
    }
    dcb.BaudRate = static_cast<DWORD>(baudRate);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    if (!SetCommState(h, &dcb)) {
        return false;
    }
    COMMTIMEOUTS t = {};
    t.ReadIntervalTimeout = MAXDWORD;
    t.ReadTotalTimeoutMultiplier = 0;
    t.ReadTotalTimeoutConstant = SERIAL_TIMEOUT_MS;
    t.WriteTotalTimeoutMultiplier = 0;
    t.WriteTotalTimeoutConstant = SERIAL_TIMEOUT_MS;
    return SetCommTimeouts(h, &t) != FALSE;
}

#endif

} // namespace

uint16_t SyringePumpService::crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x0001) {
                crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
            } else {
                crc = static_cast<uint16_t>(crc >> 1);
            }
        }
    }
    return crc;
}

std::vector<uint8_t> SyringePumpService::floatToRegisters(float value) {
    std::vector<uint8_t> result(4);
    uint8_t bytes[4];
    std::memcpy(bytes, &value, 4);
    result[0] = bytes[3];
    result[1] = bytes[2];
    result[2] = bytes[1];
    result[3] = bytes[0];
    return result;
}

float SyringePumpService::registersToFloat(const uint8_t* data) {
    uint8_t bytes[4];
    bytes[3] = data[0];
    bytes[2] = data[1];
    bytes[1] = data[2];
    bytes[0] = data[3];
    float v;
    std::memcpy(&v, bytes, 4);
    return v;
}

std::vector<uint8_t> SyringePumpService::buildReadRequest(uint8_t addr, uint16_t startReg, uint16_t count) {
    std::vector<uint8_t> frame(6);
    frame[0] = addr;
    frame[1] = FUNC_READ_HOLDING;
    frame[2] = static_cast<uint8_t>((startReg >> 8) & 0xFF);
    frame[3] = static_cast<uint8_t>(startReg & 0xFF);
    frame[4] = static_cast<uint8_t>((count >> 8) & 0xFF);
    frame[5] = static_cast<uint8_t>(count & 0xFF);
    uint16_t crc = crc16(frame.data(), 6);
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    return frame;
}

std::vector<uint8_t> SyringePumpService::buildWriteSingleRequest(uint8_t addr, uint16_t reg, uint16_t value) {
    std::vector<uint8_t> frame(6);
    frame[0] = addr;
    frame[1] = FUNC_WRITE_SINGLE;
    frame[2] = static_cast<uint8_t>((reg >> 8) & 0xFF);
    frame[3] = static_cast<uint8_t>(reg & 0xFF);
    frame[4] = static_cast<uint8_t>((value >> 8) & 0xFF);
    frame[5] = static_cast<uint8_t>(value & 0xFF);
    uint16_t crc = crc16(frame.data(), 6);
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    return frame;
}

std::vector<uint8_t> SyringePumpService::buildWriteMultipleRequest(uint8_t addr, uint16_t startReg,
                                                                   const std::vector<uint8_t>& regData) {
    uint16_t regCount = static_cast<uint16_t>(regData.size() / 2);
    uint8_t byteCount = static_cast<uint8_t>(regData.size());
    std::vector<uint8_t> frame;
    frame.reserve(7 + regData.size() + 2);
    frame.push_back(addr);
    frame.push_back(FUNC_WRITE_MULTIPLE);
    frame.push_back(static_cast<uint8_t>((startReg >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(startReg & 0xFF));
    frame.push_back(static_cast<uint8_t>((regCount >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(regCount & 0xFF));
    frame.push_back(byteCount);
    frame.insert(frame.end(), regData.begin(), regData.end());
    uint16_t crc = crc16(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    return frame;
}

bool SyringePumpService::sendRequest(int pumpIdx, const std::vector<uint8_t>& request,
                                     std::vector<uint8_t>& response, int expectedBytes) {
#ifdef _WIN32
    auto& pump = pumps_[static_cast<size_t>(pumpIdx)];
    HANDLE h = static_cast<HANDLE>(pump.serial_handle);
    if (!h || h == INVALID_HANDLE_VALUE) {
        return false;
    }

    PurgeComm(h, PURGE_RXCLEAR);

    std::this_thread::sleep_for(std::chrono::milliseconds(INTER_FRAME_DELAY_MS));

    SPDLOG_DEBUG("SyringePumpService: TX pump {} [{}]: {}", pumpIdx, request.size(), hexBytes(request));

    DWORD written = 0;
    if (!WriteFile(h, request.data(), static_cast<DWORD>(request.size()), &written, nullptr)) {
        SPDLOG_ERROR("SyringePumpService: WriteFile failed for pump {} (err={})", pumpIdx, GetLastError());
        return false;
    }
    if (written != request.size()) {
        SPDLOG_ERROR("SyringePumpService: Failed to write {} bytes to pump {} (got {})", request.size(), pumpIdx,
                     written);
        return false;
    }

    response.clear();
    while (static_cast<int>(response.size()) < expectedBytes) {
        uint8_t chunk[256];
        DWORD need = static_cast<DWORD>(expectedBytes - static_cast<int>(response.size()));
        DWORD to_read = need > 256 ? 256 : need;
        DWORD nread = 0;
        if (!ReadFile(h, chunk, to_read, &nread, nullptr)) {
            SPDLOG_ERROR("SyringePumpService: ReadFile failed for pump {} (err={})", pumpIdx, GetLastError());
            return false;
        }
        if (nread == 0) {
            SPDLOG_ERROR("SyringePumpService: Read timeout for pump {} (got {}/{} bytes): {}", pumpIdx,
                         response.size(), expectedBytes, hexBytes(response));
            return false;
        }
        response.insert(response.end(), chunk, chunk + nread);

        if (response.size() >= 5 && (response[1] & 0x80)) {
            response.resize(5);
            break;
        }
    }

    SPDLOG_DEBUG("SyringePumpService: RX pump {} [{}]: {}", pumpIdx, response.size(), hexBytes(response));

    if (response.size() < 4) {
        SPDLOG_ERROR("SyringePumpService: Response too short ({} bytes) from pump {}", response.size(), pumpIdx);
        return false;
    }
    size_t dataLen = response.size() - 2;
    uint16_t receivedCrc = static_cast<uint16_t>(
        static_cast<uint8_t>(response[response.size() - 2]) |
        (static_cast<uint16_t>(static_cast<uint8_t>(response[response.size() - 1])) << 8));
    uint16_t calculatedCrc = crc16(response.data(), dataLen);
    if (receivedCrc != calculatedCrc) {
        SPDLOG_ERROR("SyringePumpService: CRC mismatch for pump {} (received 0x{:04X}, calculated 0x{:04X})",
                     pumpIdx, receivedCrc, calculatedCrc);
        return false;
    }

    if (response[1] & 0x80) {
        uint8_t exceptionCode = response[2];
        SPDLOG_ERROR("SyringePumpService: Modbus exception 0x{:02X} from pump {} (func=0x{:02X})",
                     exceptionCode, pumpIdx, static_cast<uint8_t>(response[1]) & 0x7F);
        return false;
    }

    return true;
#else
    (void)pumpIdx;
    (void)request;
    (void)response;
    (void)expectedBytes;
    return false;
#endif
}

bool SyringePumpService::readHoldingRegisters(int pumpIdx, uint16_t startReg, uint16_t count,
                                              std::vector<uint8_t>& data) {
    auto& pump = pumps_[static_cast<size_t>(pumpIdx)];
    std::vector<uint8_t> request = buildReadRequest(pump.config.modbusAddress, startReg, count);
    int expectedBytes = 3 + count * 2 + 2;
    std::vector<uint8_t> response;
    if (!sendRequest(pumpIdx, request, response, expectedBytes)) {
        return false;
    }
    data.assign(response.begin() + 3, response.begin() + 3 + count * 2);
    return true;
}

bool SyringePumpService::writeSingleRegister(int pumpIdx, uint16_t reg, uint16_t value) {
    auto& pump = pumps_[static_cast<size_t>(pumpIdx)];
    std::vector<uint8_t> request = buildWriteSingleRequest(pump.config.modbusAddress, reg, value);
    std::vector<uint8_t> response;
    return sendRequest(pumpIdx, request, response, 8);
}

bool SyringePumpService::writeMultipleRegisters(int pumpIdx, uint16_t startReg,
                                                const std::vector<uint8_t>& regData) {
    auto& pump = pumps_[static_cast<size_t>(pumpIdx)];
    std::vector<uint8_t> request = buildWriteMultipleRequest(pump.config.modbusAddress, startReg, regData);
    std::vector<uint8_t> response;
    return sendRequest(pumpIdx, request, response, 8);
}

SyringePumpService::SyringePumpService() = default;

SyringePumpService::~SyringePumpService() {
    disconnect(PumpId::Sample);
    disconnect(PumpId::Sheath);
}

bool SyringePumpService::connect(PumpId id, int comPort, int baudRate, uint8_t modbusAddress) {
#ifdef _WIN32
    int idx = static_cast<int>(id);
    auto& pump = pumps_[static_cast<size_t>(idx)];

    disconnect(id);

    std::scoped_lock lock(pump.mutex);

    pump.config.comPort = comPort;
    pump.config.baudRate = baudRate;
    pump.config.modbusAddress = modbusAddress;

    const std::string portName = comPortName(comPort);
    HANDLE h = CreateFileA(portName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        SPDLOG_ERROR("SyringePumpService: CreateFile failed for {} (err={})", portName, GetLastError());
        return false;
    }

    if (!configureSerialHandle(h, baudRate)) {
        SPDLOG_ERROR("SyringePumpService: Failed to configure serial port {} (err={})", portName, GetLastError());
        CloseHandle(h);
        return false;
    }

    pump.serial_handle = static_cast<void*>(h);

    SPDLOG_INFO("SyringePumpService: {} opened for {} pump (baud={}, addr={})", portName, pumpName(id), baudRate,
                modbusAddress);

    if (!writeSingleRegister(idx, REG_CHANNEL_ENABLE, 1)) {
        SPDLOG_ERROR("SyringePumpService: {} pump not responding on COM{} addr={} — check wiring and address",
                     pumpName(id), comPort, modbusAddress);
        CloseHandle(static_cast<HANDLE>(pump.serial_handle));
        pump.serial_handle = nullptr;
        return false;
    }

    std::vector<uint8_t> minData;
    std::vector<uint8_t> maxData;
    if (readHoldingRegisters(idx, REG_MIN_FLOW_RATE, 2, minData) && minData.size() >= 4) {
        pump.status.minFlowRate = registersToFloat(minData.data());
        SPDLOG_INFO("SyringePumpService: {} pump min flow rate: {}", pumpName(id), pump.status.minFlowRate);
    }
    if (readHoldingRegisters(idx, REG_MAX_FLOW_RATE, 2, maxData) && maxData.size() >= 4) {
        pump.status.maxFlowRate = registersToFloat(maxData.data());
        SPDLOG_INFO("SyringePumpService: {} pump max flow rate: {}", pumpName(id), pump.status.maxFlowRate);
    }

    pump.status.connected = true;
    SPDLOG_INFO("SyringePumpService: {} pump connected on COM{}", pumpName(id), comPort);
    return true;
#else
    (void)id;
    (void)comPort;
    (void)baudRate;
    (void)modbusAddress;
    return false;
#endif
}

void SyringePumpService::disconnect(PumpId id) {
#ifdef _WIN32
    int idx = static_cast<int>(id);
    auto& pump = pumps_[static_cast<size_t>(idx)];

    std::scoped_lock lock(pump.mutex);
    if (!pump.serial_handle) {
        return;
    }

    HANDLE h = static_cast<HANDLE>(pump.serial_handle);

    if (pump.status.connected && pump.status.runStatus != RunStatus::Stop) {
        writeSingleRegister(idx, REG_RUN_COMMAND, 0);
    }

    CloseHandle(h);
    pump.serial_handle = nullptr;
    pump.status.connected = false;
    pump.status.runStatus = RunStatus::Stop;
    pump.status.currentFlowRate = 0.0;
    pump.status.accumulatedVolume = 0.0;

    SPDLOG_INFO("SyringePumpService: {} pump disconnected", pumpName(id));
#else
    (void)id;
#endif
}

bool SyringePumpService::isConnected(PumpId id) const {
    int idx = static_cast<int>(id);
    const auto& pump = pumps_[static_cast<size_t>(idx)];
    std::scoped_lock lock(pump.mutex);
    return pump.status.connected;
}

bool SyringePumpService::setFlowRate(PumpId id, double rate, uint16_t unit) {
    int idx = static_cast<int>(id);
    auto& pump = pumps_[static_cast<size_t>(idx)];
    std::scoped_lock lock(pump.mutex);

    if (!pump.status.connected) return false;

    uint16_t rateValue = static_cast<uint16_t>(std::clamp(rate, 1.0, 9999.0));

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

void SyringePumpService::pollStatus(PumpId id) {
    int idx = static_cast<int>(id);
    auto& pump = pumps_[static_cast<size_t>(idx)];
    std::scoped_lock lock(pump.mutex);

#ifdef _WIN32
    {
        HANDLE hh = static_cast<HANDLE>(pump.serial_handle);
        if (!pump.status.connected || !hh || hh == INVALID_HANDLE_VALUE) {
            return;
        }
    }
#else
    if (!pump.status.connected) {
        return;
    }
#endif

    std::vector<uint8_t> runData;
    if (readHoldingRegisters(idx, REG_RUN_COMMAND, 1, runData) && runData.size() >= 2) {
        uint16_t running = static_cast<uint16_t>(
            (static_cast<uint16_t>(runData[0]) << 8) | static_cast<uint16_t>(runData[1]));
        if (running == 0) {
            pump.status.runStatus = RunStatus::Stop;
        } else {
            std::vector<uint8_t> dirData;
            if (readHoldingRegisters(idx, REG_DIRECTION_STATUS, 1, dirData) && dirData.size() >= 2) {
                uint16_t d = static_cast<uint16_t>(
                    (static_cast<uint16_t>(dirData[0]) << 8) | static_cast<uint16_t>(dirData[1]));
                pump.status.runStatus = (d == 2) ? RunStatus::Backward : RunStatus::Forward;
            } else {
                pump.status.runStatus = RunStatus::Forward;
            }
        }
    }

    std::vector<uint8_t> errData;
    if (readHoldingRegisters(idx, REG_ERROR_STATUS, 1, errData) && errData.size() >= 2) {
        uint16_t err = static_cast<uint16_t>(
            (static_cast<uint16_t>(errData[0]) << 8) | static_cast<uint16_t>(errData[1]));
        pump.status.stalled = (err & 0x0008) != 0;
    }

    std::vector<uint8_t> flowData;
    if (readHoldingRegisters(idx, REG_REALTIME_INFUSE_FLOW, 1, flowData) && flowData.size() >= 2) {
        pump.status.currentFlowRate = static_cast<double>(
            (static_cast<uint16_t>(flowData[0]) << 8) | static_cast<uint16_t>(flowData[1]));
    }

    std::vector<uint8_t> volData;
    if (readHoldingRegisters(idx, REG_ACCUM_VOLUME, 2, volData) && volData.size() >= 4) {
        pump.status.accumulatedVolume = registersToFloat(volData.data());
    }
}

} // namespace backend::services
