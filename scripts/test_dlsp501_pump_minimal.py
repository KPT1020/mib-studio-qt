import unittest

from scripts.dlsp501_pump_minimal import (
    build_parser,
    build_read_holding,
    build_write_single,
    modbus_crc16,
)


class TestDlsp501PumpMinimal(unittest.TestCase):
    def test_crc_known_frame(self) -> None:
        payload = bytes([0x01, 0x06, 0x00, 0x01, 0x00, 0x01])
        self.assertEqual(modbus_crc16(payload), 0xCA19)

    def test_build_read_holding_frame(self) -> None:
        frame = build_read_holding(addr=1, reg=0x0001, count=1)
        self.assertEqual(frame, bytes([0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0xD5, 0xCA]))

    def test_build_write_single_frame(self) -> None:
        frame = build_write_single(addr=1, reg=0x0001, value=1)
        self.assertEqual(frame, bytes([0x01, 0x06, 0x00, 0x01, 0x00, 0x01, 0x19, 0xCA]))

    def test_parser_set_flow(self) -> None:
        parser = build_parser()
        args = parser.parse_args(
            [
                "--port",
                "COM3",
                "--addr",
                "2",
                "set-flow",
                "--rate",
                "250",
                "--unit",
                "100",
                "--direction",
                "withdraw",
            ]
        )
        self.assertEqual(args.port, "COM3")
        self.assertEqual(args.addr, 2)
        self.assertEqual(args.command, "set-flow")
        self.assertEqual(args.rate, 250)
        self.assertEqual(args.direction, "withdraw")


if __name__ == "__main__":
    unittest.main()
