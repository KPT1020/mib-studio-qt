#!/usr/bin/env python3
"""
Bare-minimum Modbus RTU script for Longer dLSP501 syringe pumps.

This intentionally avoids higher-level Modbus libraries and uses pyserial only.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from typing import List

import serial


# Register map mirrored from src/backend/services/SyringePumpService.cpp
REG_CHANNEL_ENABLE = 0x0000
REG_RUN_COMMAND = 0x0001
REG_FULL_SPEED_RUN = 0x0008
REG_ERROR_STATUS = 0x0100
REG_REALTIME_INFUSE_FLOW = 0x0102
REG_DIRECTION_STATUS = 0x010A
REG_MODE = 0x0060
REG_INFUSE_FLOW_RATE = 0x006A
REG_INFUSE_FLOW_UNIT = 0x006B
REG_WITHDRAW_FLOW_RATE = 0x006C
REG_WITHDRAW_FLOW_UNIT = 0x006D

FUNC_READ_HOLDING = 0x03
FUNC_WRITE_SINGLE = 0x06


def modbus_crc16(payload: bytes) -> int:
    crc = 0xFFFF
    for b in payload:
        crc ^= b
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def append_crc(payload: bytes) -> bytes:
    crc = modbus_crc16(payload)
    return payload + bytes((crc & 0xFF, (crc >> 8) & 0xFF))


def build_read_holding(addr: int, reg: int, count: int) -> bytes:
    body = bytes(
        (
            addr,
            FUNC_READ_HOLDING,
            (reg >> 8) & 0xFF,
            reg & 0xFF,
            (count >> 8) & 0xFF,
            count & 0xFF,
        )
    )
    return append_crc(body)


def build_write_single(addr: int, reg: int, value: int) -> bytes:
    body = bytes(
        (
            addr,
            FUNC_WRITE_SINGLE,
            (reg >> 8) & 0xFF,
            reg & 0xFF,
            (value >> 8) & 0xFF,
            value & 0xFF,
        )
    )
    return append_crc(body)


@dataclass
class PumpClient:
    port: str
    addr: int = 1
    baud: int = 115200
    timeout: float = 1.0

    def __post_init__(self) -> None:
        self._ser = serial.Serial(
            port=self.port,
            baudrate=self.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=self.timeout,
        )

    def close(self) -> None:
        self._ser.close()

    def _request(self, request: bytes, expected_len: int) -> bytes:
        self._ser.reset_input_buffer()
        self._ser.write(request)
        self._ser.flush()
        response = self._ser.read(expected_len)
        if len(response) != expected_len:
            raise RuntimeError(
                f"Short response: expected {expected_len} bytes, got {len(response)} bytes."
            )
        data, crc_lo, crc_hi = response[:-2], response[-2], response[-1]
        got_crc = (crc_hi << 8) | crc_lo
        calc_crc = modbus_crc16(data)
        if got_crc != calc_crc:
            raise RuntimeError(
                f"CRC mismatch: got 0x{got_crc:04X}, expected 0x{calc_crc:04X}"
            )
        if response[1] & 0x80:
            raise RuntimeError(
                f"Modbus exception: function=0x{response[1] & 0x7F:02X} "
                f"code=0x{response[2]:02X}"
            )
        return response

    def read_holding(self, reg: int, count: int = 1) -> List[int]:
        req = build_read_holding(self.addr, reg, count)
        expected = 3 + (count * 2) + 2
        resp = self._request(req, expected)
        byte_count = resp[2]
        if byte_count != count * 2:
            raise RuntimeError(
                f"Unexpected byte count: got {byte_count}, expected {count * 2}"
            )
        out: List[int] = []
        for i in range(count):
            hi = resp[3 + i * 2]
            lo = resp[4 + i * 2]
            out.append((hi << 8) | lo)
        return out

    def write_single(self, reg: int, value: int) -> None:
        req = build_write_single(self.addr, reg, value)
        resp = self._request(req, 8)
        if resp != req:
            raise RuntimeError("Write response does not match request echo.")

    def enable(self) -> None:
        self.write_single(REG_CHANNEL_ENABLE, 1)

    def stop(self) -> None:
        self.write_single(REG_RUN_COMMAND, 0)

    def start(self) -> None:
        self.enable()
        self.write_single(REG_RUN_COMMAND, 1)

    def set_flow(self, rate: int, unit: int, direction: str) -> None:
        if rate < 1 or rate > 9999:
            raise ValueError("Flow rate must be within 1..9999")
        dir_code = 0 if direction == "infuse" else 1
        self.write_single(REG_INFUSE_FLOW_RATE, rate)
        self.write_single(REG_INFUSE_FLOW_UNIT, unit)
        self.write_single(REG_WITHDRAW_FLOW_RATE, rate)
        self.write_single(REG_WITHDRAW_FLOW_UNIT, unit)
        self.write_single(REG_MODE, dir_code)

    def purge(self, direction: str) -> None:
        self.enable()
        value = 1 if direction == "infuse" else 2
        self.write_single(REG_FULL_SPEED_RUN, value)

    def stop_purge(self) -> None:
        self.write_single(REG_FULL_SPEED_RUN, 0)

    def status(self) -> str:
        run = self.read_holding(REG_RUN_COMMAND, 1)[0]
        direction = self.read_holding(REG_DIRECTION_STATUS, 1)[0]
        error = self.read_holding(REG_ERROR_STATUS, 1)[0]
        flow = self.read_holding(REG_REALTIME_INFUSE_FLOW, 1)[0]
        stalled = "yes" if (error & 0x0008) else "no"
        return (
            f"run={run} direction={direction} realtime_flow={flow} "
            f"error=0x{error:04X} stalled={stalled}"
        )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Minimal dLSP501 Modbus RTU controller")
    parser.add_argument("--port", required=True, help="Serial port, e.g. COM3")
    parser.add_argument("--addr", type=int, default=1, help="Modbus slave address")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument("--timeout", type=float, default=1.0, help="Serial timeout in seconds")

    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status")
    sub.add_parser("enable")
    sub.add_parser("start")
    sub.add_parser("stop")

    set_flow = sub.add_parser("set-flow")
    set_flow.add_argument("--rate", type=int, required=True, help="Flow rate 1..9999")
    set_flow.add_argument("--unit", type=int, default=100, help="Flow unit code (100=uL/min)")
    set_flow.add_argument(
        "--direction",
        choices=("infuse", "withdraw"),
        default="infuse",
        help="Pump direction",
    )

    purge = sub.add_parser("purge")
    purge.add_argument(
        "--direction",
        choices=("infuse", "withdraw"),
        default="infuse",
        help="Purge direction",
    )
    sub.add_parser("stop-purge")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    client = PumpClient(port=args.port, addr=args.addr, baud=args.baud, timeout=args.timeout)
    try:
        if args.command == "status":
            print(client.status())
        elif args.command == "enable":
            client.enable()
            print("OK: channel enabled")
        elif args.command == "start":
            client.start()
            print("OK: run command set to start")
        elif args.command == "stop":
            client.stop()
            print("OK: run command set to stop")
        elif args.command == "set-flow":
            client.set_flow(rate=args.rate, unit=args.unit, direction=args.direction)
            print(
                f"OK: flow set rate={args.rate} unit={args.unit} "
                f"direction={args.direction}"
            )
        elif args.command == "purge":
            client.purge(args.direction)
            print(f"OK: purge started direction={args.direction}")
        elif args.command == "stop-purge":
            client.stop_purge()
            print("OK: purge stopped")
        else:
            raise RuntimeError(f"Unhandled command: {args.command}")
    finally:
        client.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
