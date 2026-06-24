// modbus_rtu_test
//
// Protocol-correctness guard for the syringe pump's Modbus RTU framing
// (extracted to backend::services::modbus). A wrong CRC, byte order, or frame
// layout silently breaks every pump command, so this pins them down with
// known-answer vectors and round-trips.

#include "backend/services/ModbusRtu.h"

#include "support/assert.h"

#include <QByteArray>

#include <cstdint>
#include <string>

namespace m = backend::services::modbus;

namespace {
uint8_t at(const QByteArray& b, int i) { return static_cast<uint8_t>(b.at(i)); }

// CRC stored low-byte-first (RTU) over the first `bodyLen` bytes must match.
void expectTrailingCrc(const QByteArray& frame, int bodyLen, const char* what)
{
    MIB_REQUIRE(frame.size() == bodyLen + 2, std::string("frame length: ") + what);
    const uint16_t crc =
        m::crc16(reinterpret_cast<const uint8_t*>(frame.constData()),
                 static_cast<size_t>(bodyLen));
    MIB_EXPECT(at(frame, bodyLen) == (crc & 0xFF), std::string("CRC low byte first: ") + what);
    MIB_EXPECT(at(frame, bodyLen + 1) == ((crc >> 8) & 0xFF),
               std::string("CRC high byte second: ") + what);
}
} // namespace

int main()
{
    // 1) CRC-16/MODBUS canonical check value: "123456789" -> 0x4B37.
    {
        const char* s = "123456789";
        MIB_EXPECT(m::crc16(reinterpret_cast<const uint8_t*>(s), 9) == 0x4B37,
                   "CRC-16/MODBUS check value (0x4B37)");
    }

    // 2) Read request layout: addr|0x03|startReg(BE)|count(BE)|crc(LE).
    {
        const QByteArray f = m::buildReadRequest(0x01, 0x006A, 0x0002);
        MIB_REQUIRE(f.size() == 8, "read request is 8 bytes");
        MIB_EXPECT(at(f, 0) == 0x01, "addr");
        MIB_EXPECT(at(f, 1) == 0x03, "func read-holding");
        MIB_EXPECT(at(f, 2) == 0x00 && at(f, 3) == 0x6A, "startReg big-endian");
        MIB_EXPECT(at(f, 4) == 0x00 && at(f, 5) == 0x02, "count big-endian");
        expectTrailingCrc(f, 6, "read request");
    }

    // 3) Write-single layout: addr|0x06|reg(BE)|value(BE)|crc(LE).
    {
        const QByteArray f = m::buildWriteSingleRequest(0x01, 0x0001, 0x0001);
        MIB_REQUIRE(f.size() == 8, "write-single is 8 bytes");
        MIB_EXPECT(at(f, 1) == 0x06, "func write-single");
        MIB_EXPECT(at(f, 2) == 0x00 && at(f, 3) == 0x01, "reg big-endian");
        MIB_EXPECT(at(f, 4) == 0x00 && at(f, 5) == 0x01, "value big-endian");
        expectTrailingCrc(f, 6, "write-single");
    }

    // 4) Write-multiple layout: addr|0x10|startReg(BE)|regCount(BE)|byteCount|data|crc.
    {
        QByteArray data(4, 0);
        data[0] = 0x12; data[1] = 0x34; data[2] = 0x56; data[3] = 0x78;
        const QByteArray f = m::buildWriteMultipleRequest(0x01, 0x006A, data);
        MIB_REQUIRE(f.size() == 7 + 4 + 2, "write-multiple length = 7+data+2");
        MIB_EXPECT(at(f, 1) == 0x10, "func write-multiple");
        MIB_EXPECT(at(f, 2) == 0x00 && at(f, 3) == 0x6A, "startReg big-endian");
        MIB_EXPECT(at(f, 4) == 0x00 && at(f, 5) == 0x02, "regCount = data/2");
        MIB_EXPECT(at(f, 6) == 0x04, "byteCount = data size");
        MIB_EXPECT(at(f, 7) == 0x12 && at(f, 10) == 0x78, "payload preserved");
        expectTrailingCrc(f, 11, "write-multiple");
    }

    // 5) Float <-> registers: ABCD big-endian word order, and round-trip.
    {
        // 1.0f == 0x3F800000; ABCD (high word first) -> 3F 80 00 00.
        const QByteArray r = m::floatToRegisters(1.0f);
        MIB_REQUIRE(r.size() == 4, "float packs to 4 bytes");
        MIB_EXPECT(at(r, 0) == 0x3F && at(r, 1) == 0x80 && at(r, 2) == 0x00 && at(r, 3) == 0x00,
                   "1.0f packs ABCD as 3F 80 00 00");

        for (float v : {0.0f, 1.0f, -1.5f, 3.14159f, 9999.0f, 0.001f}) {
            const QByteArray packed = m::floatToRegisters(v);
            const float back = m::registersToFloat(
                reinterpret_cast<const uint8_t*>(packed.constData()));
            MIB_EXPECT(back == v, "float register round-trip is exact");
        }
    }

    if (mib::test::exitCode() == 0) {
        std::printf("Modbus RTU framing/CRC/float packing verified\n");
    }
    return mib::test::exitCode();
}
