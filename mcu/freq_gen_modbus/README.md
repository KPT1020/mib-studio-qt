# Frequency Generator — Modbus RTU (ESP32)

ESP32-based frequency generator controlled via Modbus RTU over serial. Used to generate a square-wave trigger signal for camera capture in the MIB Studio Qt pipeline.

## Hardware

- **Board**: ESP32 DevKit (PlatformIO `esp32dev`)
- **Serial**: 115200 baud, Modbus slave ID `1`
- **Output**: 50% duty-cycle square wave via LEDC peripheral

## Holding Registers

| Address | Name           | Access | Range     | Default | Description                    |
|---------|----------------|--------|-----------|---------|--------------------------------|
| 0       | Target freq    | R/W    | 0 -- 20000 | 5000    | Desired output frequency (Hz)  |
| 1       | Actual freq    | R      | —         | 0       | Measured LEDC frequency (Hz)   |
| 2       | Output enable  | R/W    | 0 or 1    | 1       | 1 = output on, 0 = output off |
| 3       | Output pin     | R/W    | 0 -- 39   | 32      | GPIO pin for signal output     |

### Modbus Functions

- **Read**: Function code `0x03` (Read Holding Registers)
- **Write**: Function code `0x06` (Write Single Register)

Register 1 (actual freq) is read-only; writes to it return `ILLEGAL_DATA_ADDRESS`.

## Usage with MIB Studio Qt

Connect the ESP32 to the host PC via USB-serial. From any Modbus RTU master (or a future MIB Studio integration), send commands to control the trigger frequency:

```
# Set frequency to 10 kHz (write 10000 to register 0)
Slave 1, FC 06, Addr 0, Value 10000

# Disable output (write 0 to register 2)
Slave 1, FC 06, Addr 2, Value 0

# Change output pin to GPIO 25 (write 25 to register 3)
Slave 1, FC 06, Addr 3, Value 25

# Read all registers (read 4 registers starting at address 0)
Slave 1, FC 03, Addr 0, Count 4
```

### Pin Configuration

The output pin defaults to GPIO 32 and can be changed at runtime by writing to register 3. When the pin changes, the output is stopped on the old pin and restarted on the new pin. Valid range is 0--39 (ESP32 GPIO range).

## Building and Flashing

Requires [PlatformIO](https://platformio.org/).

```bash
cd mcu/freq_gen_modbus

# Build
pio run

# Flash
pio run --target upload

# Serial monitor
pio device monitor
```

## Dependencies

- **eModbus** v1.7.4 — Modbus RTU server implementation (installed automatically by PlatformIO)
