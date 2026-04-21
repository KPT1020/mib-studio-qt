#include "backend/services/SyringePumpService.h"

#include <QByteArray>
#include <QIODevice>
#include <QSerialPort>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace backend::services {

namespace {
// Modbus RTU register addresses (from dLSP 501X manual Appendix B)
constexpr uint16_t REG_CHANNEL_ENABLE = 0x0000;   // 0=disable, 1=enable
constexpr uint16_t REG_RUN_COMMAND = 0x0001;      // 0=stop, 1=start
constexpr uint16_t REG_FULL_SPEED_RUN = 0x0008;   // 0=stop, 1=infuse, 2=withdraw
constexpr uint16_t REG_ERROR_STATUS = 0x0100;     // bit3=stall
constexpr uint16_t REG_DIRECTION_STATUS = 0x010A; // R: 1=infuse, 2=withdraw
constexpr uint16_t REG_MIN_FLOW_RATE = 0x004A;    // float32, 2 regs
constexpr uint16_t REG_MAX_FLOW_RATE = 0x004C;    // float32, 2 regs
constexpr uint16_t REG_MODE = 0x0060;             // 0=infuse, 1=withdraw
constexpr uint16_t REG_SYRINGE_VOLUME = 0x0061;   // uint16, 1~9999
constexpr uint16_t REG_SYRINGE_VOL_UNIT = 0x0062; // uint16
constexpr uint16_t REG_INFUSE_FLOW_RATE = 0x006A; // uint16, 1~9999
constexpr uint16_t REG_INFUSE_FLOW_UNIT = 0x006B; // uint16
constexpr uint16_t REG_WITHDRAW_FLOW_RATE = 0x006C;
constexpr uint16_t REG_WITHDRAW_FLOW_UNIT = 0x006D;
constexpr uint16_t REG_REALTIME_INFUSE_FLOW = 0x0102; // uint16, read-only
constexpr uint16_t REG_ACCUM_VOLUME = 0x00C7;         // float32, 2 regs

// Modbus function codes
constexpr uint8_t FUNC_READ_HOLDING = 0x03;
constexpr uint8_t FUNC_WRITE_SINGLE = 0x06;
constexpr uint8_t FUNC_WRITE_MULTIPLE = 0x10;

// Serial timeout in milliseconds
constexpr int SERIAL_TIMEOUT_MS = 1000;
constexpr int INTER_FRAME_DELAY_MS = 5;

std::string pumpDisplayName(const QString& name, SyringePumpService::PumpHandle handle) {
    if (!name.trimmed().isEmpty()) {
        return name.toStdString();
    }
    return "Pump#" + std::to_string(handle);
}

#ifdef _WIN32
QString portNameFromComPort(int comPort) {
    return QStringLiteral("COM%1").arg(comPort);
}

int comPortFromName(const QString& portName) {
    const QString name = portName.trimmed().toUpper();
    if (!name.startsWith(QStringLiteral("COM"))) {
        return -1;
    }
    bool ok = false;
    const int value = name.mid(3).toInt(&ok);
    return ok ? value : -1;
}
#endif
} // namespace

uint16_t SyringePumpService::crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]);
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 0x0001) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001)
                                 : static_cast<uint16_t>(crc >> 1);
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
    float value = 0.0f;
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
    const uint16_t crc = crc16(reinterpret_cast<const uint8_t*>(frame.constData()), 6);
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
    const uint16_t crc = crc16(reinterpret_cast<const uint8_t*>(frame.constData()), 6);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

QByteArray SyringePumpService::buildWriteMultipleRequest(uint8_t addr, uint16_t startReg, const QByteArray& regData) {
    const uint16_t regCount = static_cast<uint16_t>(regData.size() / 2);
    const uint8_t byteCount = static_cast<uint8_t>(regData.size());
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
    const uint16_t crc =
        crc16(reinterpret_cast<const uint8_t*>(frame.constData()), static_cast<size_t>(frame.size()));
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

bool SyringePumpService::sendRequest(const PumpConnection& pump, const QByteArray& request, QByteArray& response, int expectedBytes) {
    if (!pump.serial || !pump.serial->isOpen()) {
        return false;
    }

    pump.serial->readAll();
    std::this_thread::sleep_for(std::chrono::milliseconds(INTER_FRAME_DELAY_MS));

    SPDLOG_DEBUG("SyringePumpService: TX {} [{}]: {}",
                 pumpDisplayName(pump.config.name, pump.handle), request.size(), request.toHex(' ').constData());

    const qint64 written = pump.serial->write(request);
    if (written != request.size()) {
        SPDLOG_ERROR("SyringePumpService: Failed to write {} bytes to {}",
                     request.size(), pumpDisplayName(pump.config.name, pump.handle));
        return false;
    }
    if (!pump.serial->waitForBytesWritten(SERIAL_TIMEOUT_MS)) {
        SPDLOG_ERROR("SyringePumpService: Write timeout for {}", pumpDisplayName(pump.config.name, pump.handle));
        return false;
    }

    response.clear();
    int remaining = expectedBytes;
    while (remaining > 0) {
        if (!pump.serial->waitForReadyRead(SERIAL_TIMEOUT_MS)) {
            SPDLOG_ERROR("SyringePumpService: Read timeout for {} (got {}/{} bytes): {}",
                         pumpDisplayName(pump.config.name, pump.handle), response.size(), expectedBytes,
                         response.toHex(' ').constData());
            return false;
        }

        response.append(pump.serial->readAll());
        if (response.size() >= 5 && (static_cast<uint8_t>(response[1]) & 0x80)) {
            response.truncate(5);
            break;
        }
        remaining = expectedBytes - response.size();
    }

    SPDLOG_DEBUG("SyringePumpService: RX {} [{}]: {}",
                 pumpDisplayName(pump.config.name, pump.handle), response.size(), response.toHex(' ').constData());

    if (response.size() < 4) {
        SPDLOG_ERROR("SyringePumpService: Response too short ({}) from {}",
                     response.size(), pumpDisplayName(pump.config.name, pump.handle));
        return false;
    }
    const size_t dataLen = static_cast<size_t>(response.size()) - 2;
    const uint16_t receivedCrc =
        static_cast<uint16_t>((static_cast<uint8_t>(response[response.size() - 1]) << 8) |
                              static_cast<uint8_t>(response[response.size() - 2]));
    const uint16_t calculatedCrc = crc16(reinterpret_cast<const uint8_t*>(response.constData()), dataLen);
    if (receivedCrc != calculatedCrc) {
        SPDLOG_ERROR("SyringePumpService: CRC mismatch for {} (received 0x{:04X}, calculated 0x{:04X})",
                     pumpDisplayName(pump.config.name, pump.handle), receivedCrc, calculatedCrc);
        return false;
    }

    if (static_cast<uint8_t>(response[1]) & 0x80) {
        const uint8_t exceptionCode = static_cast<uint8_t>(response[2]);
        SPDLOG_ERROR("SyringePumpService: Modbus exception 0x{:02X} from {} (func=0x{:02X})",
                     exceptionCode, pumpDisplayName(pump.config.name, pump.handle), static_cast<uint8_t>(response[1]) & 0x7F);
        return false;
    }

    return true;
}

bool SyringePumpService::readHoldingRegisters(const PumpConnection& pump, uint16_t startReg, uint16_t count, QByteArray& data) {
    const QByteArray request = buildReadRequest(pump.config.modbusAddress, startReg, count);
    const int expectedBytes = 3 + count * 2 + 2;
    QByteArray response;
    if (!sendRequest(pump, request, response, expectedBytes)) {
        return false;
    }
    data = response.mid(3, count * 2);
    return true;
}

bool SyringePumpService::writeSingleRegister(const PumpConnection& pump, uint16_t reg, uint16_t value) {
    const QByteArray request = buildWriteSingleRequest(pump.config.modbusAddress, reg, value);
    QByteArray response;
    return sendRequest(pump, request, response, 8);
}

bool SyringePumpService::writeMultipleRegisters(const PumpConnection& pump, uint16_t startReg, const QByteArray& regData) {
    const QByteArray request = buildWriteMultipleRequest(pump.config.modbusAddress, startReg, regData);
    QByteArray response;
    return sendRequest(pump, request, response, 8);
}

SyringePumpService::SyringePumpService() = default;

SyringePumpService::~SyringePumpService() {
    clearPumps();
}

SyringePumpService::PumpHandle SyringePumpService::addPump(const QString& name) {
    auto pump = std::make_shared<PumpConnection>();
    pump->handle = nextHandle_.fetch_add(1);
    pump->config.name = name.trimmed().isEmpty() ? QStringLiteral("Pump %1").arg(pump->handle) : name.trimmed();

    std::scoped_lock lock(pumpsMutex_);
    pumps_.push_back(pump);
    return pump->handle;
}

bool SyringePumpService::removePump(PumpHandle handle) {
    disconnect(handle);
    std::scoped_lock lock(pumpsMutex_);
    const auto it = std::find_if(pumps_.begin(), pumps_.end(),
                                 [handle](const auto& pump) { return pump->handle == handle; });
    if (it == pumps_.end()) {
        return false;
    }
    pumps_.erase(it);
    return true;
}

void SyringePumpService::clearPumps() {
    std::vector<std::shared_ptr<PumpConnection>> pumpsCopy;
    {
        std::scoped_lock lock(pumpsMutex_);
        pumpsCopy = pumps_;
        pumps_.clear();
    }

    for (const auto& pump : pumpsCopy) {
        if (!pump) {
            continue;
        }
        std::scoped_lock lock(pump->mutex);
        if (pump->serial) {
            if (pump->serial->isOpen()) {
                pump->serial->close();
            }
            delete pump->serial;
            pump->serial = nullptr;
        }
        pump->status = PumpStatus{};
    }
}

std::vector<SyringePumpService::PumpHandle> SyringePumpService::pumpHandles() const {
    std::vector<PumpHandle> handles;
    std::scoped_lock lock(pumpsMutex_);
    handles.reserve(pumps_.size());
    for (const auto& pump : pumps_) {
        handles.push_back(pump->handle);
    }
    return handles;
}

size_t SyringePumpService::pumpCount() const {
    std::scoped_lock lock(pumpsMutex_);
    return pumps_.size();
}

bool SyringePumpService::hasPump(PumpHandle handle) const {
    return static_cast<bool>(getPump(handle));
}

std::shared_ptr<SyringePumpService::PumpConnection> SyringePumpService::getPump(PumpHandle handle) const {
    std::scoped_lock lock(pumpsMutex_);
    const auto it = std::find_if(pumps_.begin(), pumps_.end(),
                                 [handle](const auto& pump) { return pump->handle == handle; });
    if (it == pumps_.end()) {
        return nullptr;
    }
    return *it;
}

bool SyringePumpService::connect(PumpHandle handle, const QString& portName, int baudRate, uint8_t modbusAddress) {
    auto pump = getPump(handle);
    if (!pump) {
        return false;
    }

    std::scoped_lock lock(pump->mutex);
    if (pump->serial) {
        if (pump->serial->isOpen()) {
            pump->serial->close();
        }
        delete pump->serial;
        pump->serial = nullptr;
    }

    pump->config.portName = portName.trimmed();
    pump->config.baudRate = baudRate;
    pump->config.modbusAddress = modbusAddress;

    pump->serial = new QSerialPort();
    pump->serial->setPortName(pump->config.portName);
    pump->serial->setBaudRate(baudRate);
    pump->serial->setDataBits(QSerialPort::Data8);
    pump->serial->setParity(QSerialPort::NoParity);
    pump->serial->setStopBits(QSerialPort::OneStop);
    pump->serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!pump->serial->open(QIODevice::ReadWrite)) {
        SPDLOG_ERROR("SyringePumpService: Failed to open {} for {}: {}",
                     pump->config.portName.toStdString(),
                     pumpDisplayName(pump->config.name, pump->handle),
                     pump->serial->errorString().toStdString());
        delete pump->serial;
        pump->serial = nullptr;
        return false;
    }

    SPDLOG_INFO("SyringePumpService: {} opened for {} (baud={}, addr={})",
                pump->config.portName.toStdString(), pumpDisplayName(pump->config.name, pump->handle), baudRate, modbusAddress);

    if (!writeSingleRegister(*pump, REG_CHANNEL_ENABLE, 1)) {
        SPDLOG_ERROR("SyringePumpService: {} not responding on {} addr={} - check wiring/address",
                     pumpDisplayName(pump->config.name, pump->handle), pump->config.portName.toStdString(), modbusAddress);
        pump->serial->close();
        delete pump->serial;
        pump->serial = nullptr;
        pump->status = PumpStatus{};
        return false;
    }

    QByteArray minData;
    if (readHoldingRegisters(*pump, REG_MIN_FLOW_RATE, 2, minData)) {
        pump->status.minFlowRate = registersToFloat(reinterpret_cast<const uint8_t*>(minData.constData()));
    }
    QByteArray maxData;
    if (readHoldingRegisters(*pump, REG_MAX_FLOW_RATE, 2, maxData)) {
        pump->status.maxFlowRate = registersToFloat(reinterpret_cast<const uint8_t*>(maxData.constData()));
    }

    pump->status.connected = true;
    SPDLOG_INFO("SyringePumpService: {} connected on {}",
                pumpDisplayName(pump->config.name, pump->handle), pump->config.portName.toStdString());
    return true;
}

#ifdef _WIN32
bool SyringePumpService::connect(PumpHandle handle, int comPort, int baudRate, uint8_t modbusAddress) {
    return connect(handle, portNameFromComPort(comPort), baudRate, modbusAddress);
}
#endif

void SyringePumpService::disconnect(PumpHandle handle) {
    auto pump = getPump(handle);
    if (!pump) {
        return;
    }

    std::scoped_lock lock(pump->mutex);
    if (!pump->serial) {
        pump->status = PumpStatus{};
        return;
    }

    if (pump->status.connected && pump->status.runStatus != RunStatus::Stop) {
        (void)writeSingleRegister(*pump, REG_RUN_COMMAND, 0);
    }

    if (pump->serial->isOpen()) {
        pump->serial->close();
    }
    delete pump->serial;
    pump->serial = nullptr;

    pump->status = PumpStatus{};
    SPDLOG_INFO("SyringePumpService: {} disconnected", pumpDisplayName(pump->config.name, pump->handle));
}

bool SyringePumpService::isConnected(PumpHandle handle) const {
    auto pump = getPump(handle);
    if (!pump) {
        return false;
    }
    std::scoped_lock lock(pump->mutex);
    return pump->status.connected;
}

bool SyringePumpService::setFlowRate(PumpHandle handle, double rate, uint16_t unit) {
    auto pump = getPump(handle);
    if (!pump) {
        return false;
    }
    std::scoped_lock lock(pump->mutex);
    if (!pump->status.connected) {
        return false;
    }

    const uint16_t rateValue = static_cast<uint16_t>(std::clamp(rate, 1.0, 9999.0));
    if (!writeSingleRegister(*pump, REG_INFUSE_FLOW_RATE, rateValue) ||
        !writeSingleRegister(*pump, REG_INFUSE_FLOW_UNIT, unit) ||
        !writeSingleRegister(*pump, REG_WITHDRAW_FLOW_RATE, rateValue) ||
        !writeSingleRegister(*pump, REG_WITHDRAW_FLOW_UNIT, unit)) {
        SPDLOG_ERROR("SyringePumpService: Failed to set flow rate for {}", pumpDisplayName(pump->config.name, pump->handle));
        return false;
    }

    pump->config.flowRate = rate;
    pump->config.flowRateUnit = unit;
    return true;
}

bool SyringePumpService::setDirection(PumpHandle handle, Direction dir) {
    auto pump = getPump(handle);
    if (!pump) {
        return false;
    }
    std::scoped_lock lock(pump->mutex);
    if (!pump->status.connected) {
        return false;
    }
    if (!writeSingleRegister(*pump, REG_MODE, static_cast<uint16_t>(dir))) {
        SPDLOG_ERROR("SyringePumpService: Failed to set direction for {}", pumpDisplayName(pump->config.name, pump->handle));
        return false;
    }
    pump->config.direction = dir;
    return true;
}

bool SyringePumpService::start(PumpHandle handle) {
    auto pump = getPump(handle);
    if (!pump) {
        return false;
    }
    std::scoped_lock lock(pump->mutex);
    if (!pump->status.connected) {
        return false;
    }
    (void)writeSingleRegister(*pump, REG_CHANNEL_ENABLE, 1);
    return writeSingleRegister(*pump, REG_RUN_COMMAND, 1);
}

bool SyringePumpService::stop(PumpHandle handle) {
    auto pump = getPump(handle);
    if (!pump) {
        return false;
    }
    std::scoped_lock lock(pump->mutex);
    if (!pump->status.connected) {
        return false;
    }
    return writeSingleRegister(*pump, REG_RUN_COMMAND, 0);
}

bool SyringePumpService::purge(PumpHandle handle, Direction dir) {
    auto pump = getPump(handle);
    if (!pump) {
        return false;
    }
    std::scoped_lock lock(pump->mutex);
    if (!pump->status.connected) {
        return false;
    }
    (void)writeSingleRegister(*pump, REG_CHANNEL_ENABLE, 1);
    const uint16_t value = (dir == Direction::Infuse) ? 1 : 2;
    return writeSingleRegister(*pump, REG_FULL_SPEED_RUN, value);
}

bool SyringePumpService::stopPurge(PumpHandle handle) {
    auto pump = getPump(handle);
    if (!pump) {
        return false;
    }
    std::scoped_lock lock(pump->mutex);
    if (!pump->status.connected) {
        return false;
    }
    return writeSingleRegister(*pump, REG_FULL_SPEED_RUN, 0);
}

bool SyringePumpService::setSyringeVolume(PumpHandle handle, uint16_t volume, uint16_t unit) {
    auto pump = getPump(handle);
    if (!pump) {
        return false;
    }
    std::scoped_lock lock(pump->mutex);
    if (!pump->status.connected) {
        return false;
    }
    const uint16_t clampedVol = static_cast<uint16_t>(std::clamp(static_cast<int>(volume), 1, 9999));
    if (!writeSingleRegister(*pump, REG_SYRINGE_VOLUME, clampedVol) ||
        !writeSingleRegister(*pump, REG_SYRINGE_VOL_UNIT, unit)) {
        SPDLOG_ERROR("SyringePumpService: Failed to set syringe volume for {}", pumpDisplayName(pump->config.name, pump->handle));
        return false;
    }
    pump->config.syringeVolume = clampedVol;
    pump->config.syringeVolumeUnit = unit;
    return true;
}

SyringePumpService::PumpStatus SyringePumpService::getStatus(PumpHandle handle) const {
    auto pump = getPump(handle);
    if (!pump) {
        return {};
    }
    std::scoped_lock lock(pump->mutex);
    return pump->status;
}

SyringePumpService::PumpConfig SyringePumpService::getConfig(PumpHandle handle) const {
    auto pump = getPump(handle);
    if (!pump) {
        return {};
    }
    std::scoped_lock lock(pump->mutex);
    return pump->config;
}

void SyringePumpService::setConfig(PumpHandle handle, const PumpConfig& config) {
    auto pump = getPump(handle);
    if (!pump) {
        return;
    }
    std::scoped_lock lock(pump->mutex);
    pump->config = config;
    if (pump->config.name.trimmed().isEmpty()) {
        pump->config.name = QStringLiteral("Pump %1").arg(static_cast<qulonglong>(pump->handle));
    }
}

QString SyringePumpService::getPumpName(PumpHandle handle) const {
    auto pump = getPump(handle);
    if (!pump) {
        return {};
    }
    std::scoped_lock lock(pump->mutex);
    return pump->config.name;
}

void SyringePumpService::setPumpName(PumpHandle handle, const QString& name) {
    auto pump = getPump(handle);
    if (!pump) {
        return;
    }
    std::scoped_lock lock(pump->mutex);
    pump->config.name = name.trimmed();
    if (pump->config.name.isEmpty()) {
        pump->config.name = QStringLiteral("Pump %1").arg(static_cast<qulonglong>(pump->handle));
    }
}

QString SyringePumpService::getPortName(PumpHandle handle) const {
    auto pump = getPump(handle);
    if (!pump) {
        return {};
    }
    std::scoped_lock lock(pump->mutex);
    return pump->config.portName;
}

#ifdef _WIN32
int SyringePumpService::getComPort(PumpHandle handle) const {
    return comPortFromName(getPortName(handle));
}
#endif

void SyringePumpService::pollStatus(PumpHandle handle) {
    auto pump = getPump(handle);
    if (!pump) {
        return;
    }
    std::scoped_lock lock(pump->mutex);
    if (!pump->status.connected || !pump->serial || !pump->serial->isOpen()) {
        return;
    }

    QByteArray runData;
    if (readHoldingRegisters(*pump, REG_RUN_COMMAND, 1, runData)) {
        const uint16_t running =
            static_cast<uint16_t>((static_cast<uint8_t>(runData[0]) << 8) | static_cast<uint8_t>(runData[1]));
        if (running == 0) {
            pump->status.runStatus = RunStatus::Stop;
        } else {
            QByteArray dirData;
            if (readHoldingRegisters(*pump, REG_DIRECTION_STATUS, 1, dirData)) {
                const uint16_t dir = static_cast<uint16_t>(
                    (static_cast<uint8_t>(dirData[0]) << 8) | static_cast<uint8_t>(dirData[1]));
                pump->status.runStatus = (dir == 2) ? RunStatus::Backward : RunStatus::Forward;
            } else {
                pump->status.runStatus = RunStatus::Forward;
            }
        }
    }

    QByteArray errData;
    if (readHoldingRegisters(*pump, REG_ERROR_STATUS, 1, errData)) {
        const uint16_t err =
            static_cast<uint16_t>((static_cast<uint8_t>(errData[0]) << 8) | static_cast<uint8_t>(errData[1]));
        pump->status.stalled = (err & 0x0008) != 0;
    }

    QByteArray flowData;
    if (readHoldingRegisters(*pump, REG_REALTIME_INFUSE_FLOW, 1, flowData)) {
        pump->status.currentFlowRate = static_cast<double>(
            (static_cast<uint8_t>(flowData[0]) << 8) | static_cast<uint8_t>(flowData[1]));
    }

    QByteArray volData;
    if (readHoldingRegisters(*pump, REG_ACCUM_VOLUME, 2, volData)) {
        pump->status.accumulatedVolume =
            registersToFloat(reinterpret_cast<const uint8_t*>(volData.constData()));
    }
}

} // namespace backend::services
