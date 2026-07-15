# ISerialPort

> Qt-free serial transport interface for the syringe-pump Modbus RTU I/O.
> Replaced `QSerialPort` so the backend links no Qt SerialPort (epic #246).

**Source:** `include/backend/services/ISerialPort.h`,
`src/backend/services/SerialPortPosix.cpp` (termios),
`src/backend/services/SerialPortWin32.cpp` (Win32)
**Tests:** `tests/backend/serial_port_posix_loopback_test.cpp` (pty loopback),
`tests/backend/syringe_pump_fake_serial_test.cpp` (fake slave)
**Related:** [[SyringePumpService]], [[../build-and-run/Dependencies]]

## Responsibility

- Abstract byte-level serial I/O behind a pure-virtual interface so no Qt type
  crosses the backend boundary. Payloads are `std::vector<uint8_t>` (the Modbus
  framing in `ModbusRtu.h` is already vector-based).
- Fixed line settings: 8 data bits / no parity / 1 stop bit / no flow control —
  the only mode the dLSP pumps use.

## Key APIs

- `open(int comPort, int baudRate)` / `close()` / `isOpen()`
- `write(const std::vector<uint8_t>&) -> int` (bytes written, -1 on error)
- `waitForBytesWritten(int ms)`, `waitForReadyRead(int ms)`, `readAll()`
- `lastError()`
- `SerialPortFactory = std::function<std::unique_ptr<ISerialPort>()>` — the DI
  seam (mirrors `CaptureService`'s `CameraFactory`). `SyringePumpService`
  defaults it to `makePlatformSerialPort()` and lets tests inject a fake.
- `makePlatformSerialPort()` — POSIX (termios) or Win32, selected in
  `src/backend/CMakeLists.txt` (`if(WIN32)` source swap).
- `makeSerialPortForPathForTesting(path, baud)` — POSIX-only test seam that
  opens an explicit device path (e.g. a pty slave); returns nullptr on Windows.

## Gotchas

- POSIX `waitForReadyRead` uses `select()`; reads are non-blocking (`VMIN=0`,
  `O_NONBLOCK`). `waitForBytesWritten` is `tcdrain()` (no timeout param).
- POSIX `open(comPort)` maps to `/dev/ttyUSB<n>` (best-effort; real deployments
  are Windows). Tests bypass this via the path seam.
- Win32 `waitForReadyRead` polls `ClearCommError().cbInQue`; `\\.\COMn` form is
  used so COM10+ work.
- Adding a new mode (parity/flow control) means extending the interface, not
  reaching for Qt.
