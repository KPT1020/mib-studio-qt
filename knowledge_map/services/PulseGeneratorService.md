# PulseGeneratorService

> Controls the Zhongsheng (中盛科技) pulse frequency & duty-cycle output module
> over RS485 Modbus RTU — the TTL pulse source for the MindVision camera's
> **external acquisition trigger** (NOT the sort-output pulse; that is
> [[TriggerService]]).

**Source:** `src/backend/services/PulseGeneratorService.cpp`,
`include/backend/services/PulseGeneratorService.h`
**Tests:** `tests/backend/pulse_generator_frame_test.cpp` (known-answer frames
from the vendor manual)
**Related:** [[SyringePumpService]] (pattern origin), [[../camera/MindVisionCamera]],
[[../frontend/ConfigTabs]], [[../architecture/AppBackend]]

## Responsibility

- One `QSerialPort` connection (blocking I/O, mutex-guarded — the
  [[SyringePumpService]] pattern) to the module.
- `connect(comPort, baud, addr)` verifies the device by reading all channel
  registers (FC03) and seeds state from hardware **without writing** — an
  already-pulsing generator keeps pulsing across a control-link reconnect.
- `setFrequency(ch, hz)` — clamps to the module's 400 Hz–40 kHz range, writes
  u32 = Hz×100 (high word first) via FC16.
- `setDutyCycle(ch, %)` — clamps 0–100, u16 = %×100 via FC06. While the
  channel is gated off, the value is cached and written on the next enable.
- `setOutputEnabled(ch, on)` — the module has **no run/stop register**: off
  writes duty 0 % (line idles low), on restores the configured duty.
- `disconnect()` closes the port and deliberately leaves outputs untouched.

## Device protocol (vendor manual 脉冲频率与占空比输出系列 V2.0)

- Modbus RTU, factory default address 1, 9600 8N1; FC 03/04/06/10.
- Channel N (0-based): frequency high word at holding register `3N`, low word
  at `3N+1` (u32 = Hz×100), duty at `3N+2` (u16 = %×100).
- Range 400 Hz–40 kHz, duty 0–100 %, resolution 0.01 Hz / 0.01 %; optocoupler
  outputs, 3.3/5 V high level via internal jumper (Y1–Y4 vs COM−).
- Device params (persisted, power-cycle to apply): 0x32 station address,
  0x33 baud code, 0x3D parity — not exposed by the service.
- Known-answer vectors (manual §2.3, pinned in the test): ch1 1000 Hz →
  `01 10 00 00 00 02 04 00 01 86 A0 C0 77`; ch1 50 % → `01 06 00 02 13 88 25 5C`.

## Encoding helpers

`clampFrequency` / `clampDuty` / `frequencyToRegisterValue` /
`dutyToRegisterValue` / `buildFrequencyFrame` / `buildDutyFrame` are pure
statics (unit-testable without a serial port); framing reuses
`backend::services::modbus::*` from `ModbusRtu.h`.

## Wiring

Owned by [[../architecture/AppBackend]] (`pulseGenerator()` accessor), driven
synchronously from the MindVision section of [[../frontend/ConfigTabs]]
(connect, frequency/duty, start/stop). Compiles on every platform — it has no
MindVision SDK dependency.
