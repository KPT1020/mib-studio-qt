// Modbus RTU framing primitives, extracted from SyringePumpService so the
// protocol logic (CRC, frame layout, float<->register packing) can be unit
// tested without a serial port. These are pure functions — no device I/O.
#pragma once

#include <QByteArray>

#include <cstdint>
#include <cstring>

namespace backend::services::modbus {

// Standard Modbus function codes.
inline constexpr uint8_t kFuncReadHolding = 0x03;
inline constexpr uint8_t kFuncWriteSingle = 0x06;
inline constexpr uint8_t kFuncWriteMultiple = 0x10;

// CRC-16 with the reflected Modbus polynomial (0xA001).
inline uint16_t crc16(const uint8_t* data, size_t len)
{
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

inline void appendCrc(QByteArray& frame)
{
    const uint16_t crc =
        crc16(reinterpret_cast<const uint8_t*>(frame.constData()),
              static_cast<size_t>(frame.size()));
    frame.append(static_cast<char>(crc & 0xFF));        // CRC low byte first (RTU)
    frame.append(static_cast<char>((crc >> 8) & 0xFF)); // CRC high byte
}

// float32 -> 2 registers (4 bytes), ABCD big-endian word order: high word
// first. Inverse of registersToFloat.
inline QByteArray floatToRegisters(float value)
{
    QByteArray result(4, 0);
    uint8_t bytes[4];
    std::memcpy(bytes, &value, 4);
    result[0] = static_cast<char>(bytes[3]);
    result[1] = static_cast<char>(bytes[2]);
    result[2] = static_cast<char>(bytes[1]);
    result[3] = static_cast<char>(bytes[0]);
    return result;
}

// 4 bytes (ABCD big-endian word order) -> float32.
inline float registersToFloat(const uint8_t* data)
{
    uint8_t bytes[4];
    bytes[3] = data[0];
    bytes[2] = data[1];
    bytes[1] = data[2];
    bytes[0] = data[3];
    float value;
    std::memcpy(&value, bytes, 4);
    return value;
}

// FC03 read-holding-registers request: [addr|0x03|startReg(BE)|count(BE)|crc(LE)]
inline QByteArray buildReadRequest(uint8_t addr, uint16_t startReg, uint16_t count)
{
    QByteArray frame(6, 0);
    frame[0] = static_cast<char>(addr);
    frame[1] = static_cast<char>(kFuncReadHolding);
    frame[2] = static_cast<char>((startReg >> 8) & 0xFF);
    frame[3] = static_cast<char>(startReg & 0xFF);
    frame[4] = static_cast<char>((count >> 8) & 0xFF);
    frame[5] = static_cast<char>(count & 0xFF);
    appendCrc(frame);
    return frame;
}

// FC06 write-single-register request: [addr|0x06|reg(BE)|value(BE)|crc(LE)]
inline QByteArray buildWriteSingleRequest(uint8_t addr, uint16_t reg, uint16_t value)
{
    QByteArray frame(6, 0);
    frame[0] = static_cast<char>(addr);
    frame[1] = static_cast<char>(kFuncWriteSingle);
    frame[2] = static_cast<char>((reg >> 8) & 0xFF);
    frame[3] = static_cast<char>(reg & 0xFF);
    frame[4] = static_cast<char>((value >> 8) & 0xFF);
    frame[5] = static_cast<char>(value & 0xFF);
    appendCrc(frame);
    return frame;
}

// FC16 write-multiple-registers request:
// [addr|0x10|startReg(BE)|regCount(BE)|byteCount|data...|crc(LE)]
inline QByteArray buildWriteMultipleRequest(uint8_t addr, uint16_t startReg,
                                            const QByteArray& regData)
{
    const uint16_t regCount = static_cast<uint16_t>(regData.size() / 2);
    const uint8_t byteCount = static_cast<uint8_t>(regData.size());
    QByteArray frame;
    frame.reserve(7 + regData.size() + 2);
    frame.append(static_cast<char>(addr));
    frame.append(static_cast<char>(kFuncWriteMultiple));
    frame.append(static_cast<char>((startReg >> 8) & 0xFF));
    frame.append(static_cast<char>(startReg & 0xFF));
    frame.append(static_cast<char>((regCount >> 8) & 0xFF));
    frame.append(static_cast<char>(regCount & 0xFF));
    frame.append(static_cast<char>(byteCount));
    frame.append(regData);
    appendCrc(frame);
    return frame;
}

} // namespace backend::services::modbus
