#include "backend/services/SyringePumpService.h"

#include <QSerialPort>
#include <QByteArray>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace backend::services {

namespace {
    // Modbus RTU register addresses (from dLSP 501X manual Appendix B)
    constexpr uint16_t REG_CHANNEL_ENABLE    = 0x0000;  // 0=disable, 1=enable
    constexpr uint16_t REG_RUN_COMMAND       = 0x0001;  // 0=stop, 1=start
    constexpr uint16_t REG_FULL_SPEED_RUN    = 0x0008;  // 0=stop, 1=full-speed infuse, 2=full-speed withdraw
    constexpr uint16_t REG_ERROR_STATUS      = 0x0100;  // bit3=stall/blockage
    constexpr uint16_t REG_DIRECTION_STATUS  = 0x010A;  // 0=none, 1=infuse, 2=withdraw
    constexpr uint16_t REG_MIN_FLOW_RATE     = 0x004A;  // float32, 2 regs
    constexpr uint16_t REG_MAX_FLOW_RATE     = 0x004C;  // float32, 2 regs
    constexpr uint16_t REG_MODE              = 0x0060;  // 0=infuse, 1=withdraw, ...
    constexpr uint16_t REG_SYRINGE_VOLUME    = 0x0061;
    constexpr uint16_t REG_SYRINGE_VOL_UNIT  = 0x0062;
    constexpr uint16_t REG_INFUSE_FLOW_RATE  = 0x006A;
    constexpr uint16_t REG_INFUSE_FLOW_UNIT  = 0x006B;
    constexpr uint16_t REG_WITHDRAW_FLOW_RATE= 0x006C;
    constexpr uint16_t REG_WITHDRAW_FLOW_UNIT= 0x006D;
    constexpr uint16_t REG_REALTIME_INFUSE_FLOW = 0x0102;
    constexpr uint16_t REG_ACCUM_VOLUME      = 0x00C7;

    constexpr uint8_t FUNC_READ_HOLDING      = 0x03;
    constexpr uint8_t FUNC_WRITE_SINGLE      = 0x06;
    constexpr uint8_t FUNC_WRITE_MULTIPLE    = 0x10;

    constexpr int SERIAL_TIMEOUT_MS = 1000;
    constexpr int INTER_FRAME_DELAY_MS = 5;
} // namespace

// ---------------------------------------------------------------------------
// CRC16 — standard Modbus polynomial (0xA001 reflected)
// ---------------------------------------------------------------------------
uint16_t SyringePumpService::crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

QByteArray SyringePumpService::floatToRegisters(float value) {
    QByteArray result(4, 0);
    uint8_t bytes[4];
    std::memcpy(bytes, &value, 4);
    result[0] = static_cast<char>(bytes[3]);
    result[1] = static_cast<char>(bytes[2]);
    result[2] = static_cast<char>(bytes[1]);
    result[3] = static_cast<char>(bytes[0]);
    return result;
}

float SyringePumpService::registersToFloat(const uint8_t* data) {
    uint8_t bytes[4];
    bytes[3] = data[0];
    bytes[2] = data[1];
    bytes[1] = data[2];
    bytes[0] = data[3];
    float value;
    std::memcpy(&value, bytes, 4);
    return value;
}

QByteArray SyringePumpService::buildReadRequest(uint8_t addr, uint16_t startReg, uint16_t count) {
    QByteArray frame(6, 0);
    frame[0] = static_cast<char>(addr);
    frame[1] = static_cast<char>(FUNC_READ_HOLDING);
    frame[2] = static_cast<char>((startReg >> 8) & 0xFF);
    frame[3] = static_cast<char>(startReg & 0xFF);
    frame[4] = static_cast<char>((count >> 8) & 0xFF);
    frame[5] = static_cast<char>(count & 0xFF);
    uint16_t crc = crc16(reinterpret_cast<const uint8_t*>(frame.constData()), 6);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

QByteArray SyringePumpService::buildWriteSingleRequest(uint8_t addr, uint16_t reg, uint16_t value) {
    QByteArray frame(6, 0);
    frame[0] = static_cast<char>(addr);
    frame[1] = static_cast<char>(FUNC_WRITE_SINGLE);
    frame[2] = static_cast<char>((reg >> 8) & 0xFF);
    frame[3] = static_cast<char>(reg & 0xFF);
    frame[4] = static_cast<char>((value >> 8) & 0xFF);
    frame[5] = static_cast<char>(value & 0xFF);
    uint16_t crc = crc16(reinterpret_cast<const uint8_t*>(frame.constData()), 6);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

QByteArray SyringePumpService::buildWriteMultipleRequest(uint8_t addr, uint16_t startReg, const QByteArray& regData) {
    uint16_t regCount = static_cast<uint16_t>(regData.size() / 2);
    uint8_t byteCount = static_cast<uint8_t>(regData.size());
    QByteArray frame;
    frame.reserve(7 + regData.size() + 2);
    frame.append(static_cast<char>(addr));
    frame.append(static_cast<char>(FUNC_WRITE_MULTIPLE));
    frame.append(static_cast<char>((startReg >> 8) & 0xFF));
    frame.append(static_cast<char>(startReg & 0xFF));
    frame.append(static_cast<char>((regCount >> 8) & 0xFF));
    frame.append(static_cast<char>(regCount & 0xFF));
    frame.append(static_cast<char>(byteCount));
    frame.append(regData);
    uint16_t crc = crc16(reinterpret_cast<const uint8_t*>(frame.constData()),
                        static_cast<size_t>(frame.size()));
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

// ---------------------------------------------------------------------------
// Serial send/receive
// ---------------------------------------------------------------------------
bool SyringePumpService::sendRequest(Pump& pump, const QByteArray& request,
                                     QByteArray& response, int expectedBytes) {
    if (!pump.serial || !pump.serial->isOpen()) {
        return false;
    }

    pump.serial->readAll();
    std::this_thread::sleep_for(std::chrono::milliseconds(INTER_FRAME_DELAY_MS));

    SPDLOG_DEBUG("SyringePumpService: TX [{}]: {}",
                 request.size(), request.toHex(' ').constData());

    qint64 written = pump.serial->write(request);
    if (written != request.size()) {
        SPDLOG_ERROR("SyringePumpService: Failed to write {} bytes to {}",
                     request.size(), pump.name);
        return false;
    }
    if (!pump.serial->waitForBytesWritten(SERIAL_TIMEOUT_MS)) {
        SPDLOG_ERROR("SyringePumpService: Write timeout for {}", pump.name);
        return false;
    }

    response.clear();
    int remaining = expectedBytes;
    while (remaining > 0) {
        if (!pump.serial->waitForReadyRead(SERIAL_TIMEOUT_MS)) {
            SPDLOG_ERROR("SyringePumpService: Read timeout for {} (got {}/{} bytes): {}",
                         pump.name, response.size(), expectedBytes,
                         response.toHex(' ').constData());
            return false;
        }
        QByteArray chunk = pump.serial->readAll();
        response.append(chunk);

        if (response.size() >= 5 && (static_cast<uint8_t>(response[1]) & 0x80)) {
            response.truncate(5);
            break;
        }
        remaining = expectedBytes - response.size();
    }

    SPDLOG_DEBUG("SyringePumpService: RX [{}]: {}",
                 response.size(), response.toHex(' ').constData());

    if (response.size() < 4) {
        SPDLOG_ERROR("SyringePumpService: Response too short ({} bytes) from {}",
                     response.size(), pump.name);
        return false;
    }
    size_t dataLen = static_cast<size_t>(response.size()) - 2;
    uint16_t receivedCrc = static_cast<uint16_t>(
        (static_cast<uint8_t>(response[response.size() - 1]) << 8) |
         static_cast<uint8_t>(response[response.size() - 2]));
    uint16_t calculatedCrc = crc16(reinterpret_cast<const uint8_t*>(response.constData()), dataLen);
    if (receivedCrc != calculatedCrc) {
        SPDLOG_ERROR("SyringePumpService: CRC mismatch for {} (received 0x{:04X}, calc 0x{:04X})",
                     pump.name, receivedCrc, calculatedCrc);
        return false;
    }

    if (static_cast<uint8_t>(response[1]) & 0x80) {
        uint8_t exceptionCode = static_cast<uint8_t>(response[2]);
        SPDLOG_ERROR("SyringePumpService: Modbus exception 0x{:02X} from {} (func=0x{:02X})",
                     exceptionCode, pump.name, static_cast<uint8_t>(response[1]) & 0x7F);
        return false;
    }
    return true;
}

bool SyringePumpService::readHoldingRegisters(Pump& pump, uint16_t startReg,
                                              uint16_t count, QByteArray& data) {
    QByteArray request = buildReadRequest(pump.config.modbusAddress, startReg, count);
    int expectedBytes = 3 + count * 2 + 2;
    QByteArray response;
    if (!sendRequest(pump, request, response, expectedBytes)) {
        return false;
    }
    data = response.mid(3, count * 2);
    return true;
}

bool SyringePumpService::writeSingleRegister(Pump& pump, uint16_t reg, uint16_t value) {
    QByteArray request = buildWriteSingleRequest(pump.config.modbusAddress, reg, value);
    QByteArray response;
    return sendRequest(pump, request, response, 8);
}

bool SyringePumpService::writeMultipleRegisters(Pump& pump, uint16_t startReg,
                                                const QByteArray& regData) {
    QByteArray request = buildWriteMultipleRequest(pump.config.modbusAddress, startReg, regData);
    QByteArray response;
    return sendRequest(pump, request, response, 8);
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
SyringePumpService::SyringePumpService() = default;

SyringePumpService::~SyringePumpService() {
    // Disconnect and destroy every pump.
    std::vector<PumpHandle> handles;
    {
        std::scoped_lock lk(pumpsMutex_);
        handles.reserve(pumps_.size());
        for (const auto& [id, _] : pumps_) handles.push_back(id);
    }
    for (auto h : handles) removePump(h);
}

// ---------------------------------------------------------------------------
// Pump lifecycle
// ---------------------------------------------------------------------------
std::shared_ptr<SyringePumpService::Pump>
SyringePumpService::findPump(PumpHandle id) const {
    std::scoped_lock lk(pumpsMutex_);
    auto it = pumps_.find(id);
    if (it == pumps_.end()) return nullptr;
    return it->second;
}

SyringePumpService::PumpHandle SyringePumpService::addPump(const std::string& name) {
    std::scoped_lock lk(pumpsMutex_);
    PumpHandle id = nextHandle_++;
    auto pump = std::make_shared<Pump>();
    pump->name = name.empty() ? ("Pump " + std::to_string(id + 1)) : name;
    pumps_.emplace(id, pump);
    SPDLOG_INFO("SyringePumpService: added pump handle={} name=\"{}\"", id, pump->name);
    return id;
}

bool SyringePumpService::removePump(PumpHandle id) {
    std::shared_ptr<Pump> pump;
    {
        std::scoped_lock lk(pumpsMutex_);
        auto it = pumps_.find(id);
        if (it == pumps_.end()) return false;
        pump = std::move(it->second);
        pumps_.erase(it);
    }
    // Disconnect without holding the map mutex.
    if (pump) {
        std::scoped_lock plk(pump->mutex);
        if (pump->serial) {
            if (pump->serial->isOpen()) pump->serial->close();
            delete pump->serial;
            pump->serial = nullptr;
        }
        SPDLOG_INFO("SyringePumpService: removed pump handle={} name=\"{}\"", id, pump->name);
    }
    return true;
}

int SyringePumpService::pumpCount() const {
    std::scoped_lock lk(pumpsMutex_);
    return static_cast<int>(pumps_.size());
}

std::vector<SyringePumpService::PumpHandle> SyringePumpService::pumpHandles() const {
    std::scoped_lock lk(pumpsMutex_);
    std::vector<PumpHandle> out;
    out.reserve(pumps_.size());
    for (const auto& [id, _] : pumps_) out.push_back(id);
    return out;
}

std::string SyringePumpService::pumpName(PumpHandle id) const {
    auto pump = findPump(id);
    if (!pump) return {};
    std::scoped_lock lk(pump->mutex);
    return pump->name;
}

void SyringePumpService::setPumpName(PumpHandle id, const std::string& name) {
    auto pump = findPump(id);
    if (!pump) return;
    std::scoped_lock lk(pump->mutex);
    pump->name = name;
}

// ---------------------------------------------------------------------------
// Connection management
// ---------------------------------------------------------------------------
bool SyringePumpService::connect(PumpHandle id, const QString& portName,
                                 int baudRate, uint8_t modbusAddress) {
    auto pump = findPump(id);
    if (!pump) {
        SPDLOG_ERROR("SyringePumpService::connect: invalid handle {}", id);
        return false;
    }

    // Close any prior connection first.
    {
        std::scoped_lock lk(pump->mutex);
        if (pump->serial && pump->serial->isOpen()) {
            pump->serial->close();
        }
        if (pump->serial) {
            delete pump->serial;
            pump->serial = nullptr;
        }
        pump->status.connected = false;
    }

    std::scoped_lock lk(pump->mutex);

    pump->config.portName = portName;
    pump->config.baudRate = baudRate;
    pump->config.modbusAddress = modbusAddress;

    pump->serial = new QSerialPort();
    pump->serial->setPortName(portName);
    pump->serial->setBaudRate(baudRate);
    pump->serial->setDataBits(QSerialPort::Data8);
    pump->serial->setParity(QSerialPort::NoParity);
    pump->serial->setStopBits(QSerialPort::OneStop);
    pump->serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!pump->serial->open(QIODevice::ReadWrite)) {
        SPDLOG_ERROR("SyringePumpService: Failed to open {} for {}: {}",
                     portName.toStdString(), pump->name,
                     pump->serial->errorString().toStdString());
        delete pump->serial;
        pump->serial = nullptr;
        return false;
    }
    SPDLOG_INFO("SyringePumpService: {} opened for {} (baud={}, addr={})",
                portName.toStdString(), pump->name, baudRate, modbusAddress);

    if (!writeSingleRegister(*pump, REG_CHANNEL_ENABLE, 1)) {
        SPDLOG_ERROR("SyringePumpService: {} not responding on {} addr={} — check wiring and address",
                     pump->name, portName.toStdString(), modbusAddress);
        pump->serial->close();
        delete pump->serial;
        pump->serial = nullptr;
        return false;
    }

    QByteArray minData, maxData;
    if (readHoldingRegisters(*pump, REG_MIN_FLOW_RATE, 2, minData)) {
        pump->status.minFlowRate = registersToFloat(
            reinterpret_cast<const uint8_t*>(minData.constData()));
    }
    if (readHoldingRegisters(*pump, REG_MAX_FLOW_RATE, 2, maxData)) {
        pump->status.maxFlowRate = registersToFloat(
            reinterpret_cast<const uint8_t*>(maxData.constData()));
    }

    pump->status.connected = true;
    SPDLOG_INFO("SyringePumpService: {} connected on {}",
                pump->name, portName.toStdString());
    return true;
}

bool SyringePumpService::connect(PumpHandle id, int comPort, int baudRate,
                                 uint8_t modbusAddress) {
    return connect(id, QString("COM%1").arg(comPort), baudRate, modbusAddress);
}

void SyringePumpService::disconnect(PumpHandle id) {
    auto pump = findPump(id);
    if (!pump) return;
    std::scoped_lock lk(pump->mutex);
    if (!pump->serial) return;

    if (pump->status.connected && pump->status.runStatus != RunStatus::Stop) {
        writeSingleRegister(*pump, REG_RUN_COMMAND, 0);
    }
    if (pump->serial->isOpen()) pump->serial->close();
    delete pump->serial;
    pump->serial = nullptr;
    pump->status.connected = false;
    pump->status.runStatus = RunStatus::Stop;
    pump->status.currentFlowRate = 0.0;
    pump->status.accumulatedVolume = 0.0;

    SPDLOG_INFO("SyringePumpService: {} disconnected", pump->name);
}

bool SyringePumpService::isConnected(PumpHandle id) const {
    auto pump = findPump(id);
    if (!pump) return false;
    std::scoped_lock lk(pump->mutex);
    return pump->status.connected;
}

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------
bool SyringePumpService::setFlowRate(PumpHandle id, double rate, uint16_t unit) {
    auto pump = findPump(id);
    if (!pump) return false;
    std::scoped_lock lk(pump->mutex);
    if (!pump->status.connected) return false;

    uint16_t rateValue = static_cast<uint16_t>(std::clamp(rate, 1.0, 9999.0));
    if (!writeSingleRegister(*pump, REG_INFUSE_FLOW_RATE, rateValue)) return false;
    if (!writeSingleRegister(*pump, REG_INFUSE_FLOW_UNIT, unit))      return false;
    if (!writeSingleRegister(*pump, REG_WITHDRAW_FLOW_RATE, rateValue)) return false;
    if (!writeSingleRegister(*pump, REG_WITHDRAW_FLOW_UNIT, unit))    return false;

    pump->config.flowRate = rate;
    pump->config.flowRateUnit = unit;
    SPDLOG_INFO("SyringePumpService: {} flow rate set to {} (unit={})",
                pump->name, rateValue, unit);
    return true;
}

bool SyringePumpService::setDirection(PumpHandle id, Direction dir) {
    auto pump = findPump(id);
    if (!pump) return false;
    std::scoped_lock lk(pump->mutex);
    if (!pump->status.connected) return false;

    if (!writeSingleRegister(*pump, REG_MODE, static_cast<uint16_t>(dir))) return false;
    pump->config.direction = dir;
    return true;
}

bool SyringePumpService::start(PumpHandle id) {
    auto pump = findPump(id);
    if (!pump) return false;
    std::scoped_lock lk(pump->mutex);
    if (!pump->status.connected) return false;

    writeSingleRegister(*pump, REG_CHANNEL_ENABLE, 1);
    if (!writeSingleRegister(*pump, REG_RUN_COMMAND, 1)) return false;
    SPDLOG_INFO("SyringePumpService: {} started", pump->name);
    return true;
}

bool SyringePumpService::stop(PumpHandle id) {
    auto pump = findPump(id);
    if (!pump) return false;
    std::scoped_lock lk(pump->mutex);
    if (!pump->status.connected) return false;

    if (!writeSingleRegister(*pump, REG_RUN_COMMAND, 0)) return false;
    SPDLOG_INFO("SyringePumpService: {} stopped", pump->name);
    return true;
}

bool SyringePumpService::purge(PumpHandle id, Direction dir) {
    auto pump = findPump(id);
    if (!pump) return false;
    std::scoped_lock lk(pump->mutex);
    if (!pump->status.connected) return false;

    writeSingleRegister(*pump, REG_CHANNEL_ENABLE, 1);
    uint16_t value = (dir == Direction::Infuse) ? 1 : 2;
    if (!writeSingleRegister(*pump, REG_FULL_SPEED_RUN, value)) return false;
    return true;
}

bool SyringePumpService::stopPurge(PumpHandle id) {
    auto pump = findPump(id);
    if (!pump) return false;
    std::scoped_lock lk(pump->mutex);
    if (!pump->status.connected) return false;
    return writeSingleRegister(*pump, REG_FULL_SPEED_RUN, 0);
}

bool SyringePumpService::setSyringeVolume(PumpHandle id, uint16_t volume, uint16_t unit) {
    auto pump = findPump(id);
    if (!pump) return false;
    std::scoped_lock lk(pump->mutex);

    uint16_t clampedVol = static_cast<uint16_t>(std::clamp(static_cast<int>(volume), 1, 9999));
    pump->config.syringeVolume = clampedVol;
    pump->config.syringeVolumeUnit = unit;

    if (!pump->status.connected) return true;  // cached only; push when connected

    if (!writeSingleRegister(*pump, REG_SYRINGE_VOLUME, clampedVol)) return false;
    if (!writeSingleRegister(*pump, REG_SYRINGE_VOL_UNIT, unit))    return false;
    return true;
}

// ---------------------------------------------------------------------------
// Status / config
// ---------------------------------------------------------------------------
SyringePumpService::PumpStatus SyringePumpService::getStatus(PumpHandle id) const {
    auto pump = findPump(id);
    if (!pump) return {};
    std::scoped_lock lk(pump->mutex);
    return pump->status;
}

SyringePumpService::PumpConfig SyringePumpService::getConfig(PumpHandle id) const {
    auto pump = findPump(id);
    if (!pump) return {};
    std::scoped_lock lk(pump->mutex);
    return pump->config;
}

void SyringePumpService::setConfig(PumpHandle id, const PumpConfig& config) {
    auto pump = findPump(id);
    if (!pump) return;
    std::scoped_lock lk(pump->mutex);
    pump->config = config;
}

QString SyringePumpService::getPortName(PumpHandle id) const {
    auto pump = findPump(id);
    if (!pump) return {};
    std::scoped_lock lk(pump->mutex);
    return pump->status.connected ? pump->config.portName : QString{};
}

QStringList SyringePumpService::reservedPortNames(PumpHandle exclude) const {
    std::vector<std::shared_ptr<Pump>> snapshot;
    {
        std::scoped_lock lk(pumpsMutex_);
        snapshot.reserve(pumps_.size());
        for (const auto& [id, p] : pumps_) {
            if (id == exclude) continue;
            snapshot.push_back(p);
        }
    }
    QStringList out;
    for (const auto& p : snapshot) {
        std::scoped_lock lk(p->mutex);
        if (p->status.connected && !p->config.portName.isEmpty()) {
            out.append(p->config.portName);
        }
    }
    return out;
}

void SyringePumpService::pollStatus(PumpHandle id) {
    auto pump = findPump(id);
    if (!pump) return;
    std::scoped_lock lk(pump->mutex);
    if (!pump->status.connected || !pump->serial || !pump->serial->isOpen()) {
        return;
    }

    QByteArray runData;
    if (readHoldingRegisters(*pump, REG_RUN_COMMAND, 1, runData)) {
        uint16_t running = static_cast<uint16_t>(
            (static_cast<uint8_t>(runData[0]) << 8) | static_cast<uint8_t>(runData[1]));
        if (running == 0) {
            pump->status.runStatus = RunStatus::Stop;
        } else {
            QByteArray dirData;
            if (readHoldingRegisters(*pump, REG_DIRECTION_STATUS, 1, dirData)) {
                uint16_t dir = static_cast<uint16_t>(
                    (static_cast<uint8_t>(dirData[0]) << 8) | static_cast<uint8_t>(dirData[1]));
                pump->status.runStatus = (dir == 2) ? RunStatus::Backward : RunStatus::Forward;
            } else {
                pump->status.runStatus = RunStatus::Forward;
            }
        }
    }

    QByteArray errData;
    if (readHoldingRegisters(*pump, REG_ERROR_STATUS, 1, errData)) {
        uint16_t err = static_cast<uint16_t>(
            (static_cast<uint8_t>(errData[0]) << 8) | static_cast<uint8_t>(errData[1]));
        pump->status.stalled = (err & 0x0008) != 0;
    }

    QByteArray flowData;
    if (readHoldingRegisters(*pump, REG_REALTIME_INFUSE_FLOW, 1, flowData)) {
        pump->status.currentFlowRate = static_cast<double>(
            (static_cast<uint8_t>(flowData[0]) << 8) | static_cast<uint8_t>(flowData[1]));
    }

    QByteArray volData;
    if (readHoldingRegisters(*pump, REG_ACCUM_VOLUME, 2, volData)) {
        pump->status.accumulatedVolume = registersToFloat(
            reinterpret_cast<const uint8_t*>(volData.constData()));
    }
}

} // namespace backend::services
