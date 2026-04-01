#include <Arduino.h>
#include "ModbusServerRTU.h"

// ── Configuration ──────────────────────────────────────────────────────────────
constexpr uint8_t  PIN          = 26;
constexpr uint8_t  LEDC_CH      = 0;
constexpr uint8_t  LEDC_RES     = 8;
constexpr uint16_t DUTY_50      = 128;       // 50% of 2^8
constexpr uint16_t FREQ_MAX     = 20000;
constexpr uint16_t FREQ_DEFAULT = 5000;
constexpr uint8_t  MB_ID        = 1;
constexpr uint32_t BAUD         = 115200;

// ── Holding registers ──────────────────────────────────────────────────────────
// HR0: target frequency Hz  (R/W, 0–20000)
// HR1: actual frequency Hz  (R)
// HR2: output enable        (R/W, 0 or 1)
constexpr uint16_t HR_COUNT = 3;
volatile uint16_t  hr[HR_COUNT] = {FREQ_DEFAULT, 0, 1};
volatile bool      hr_dirty     = false;

// ── State ──────────────────────────────────────────────────────────────────────
bool output_on = false;
ModbusServerRTU mb(2000);

// ── Frequency output control ───────────────────────────────────────────────────
void startOutput(uint16_t freq) {
  if (!output_on) {
    ledcSetup(LEDC_CH, freq, LEDC_RES);
    ledcAttachPin(PIN, LEDC_CH);
    output_on = true;
  } else {
    ledcSetup(LEDC_CH, freq, LEDC_RES);
  }
  ledcWrite(LEDC_CH, DUTY_50);
  hr[1] = (uint16_t)ledcReadFreq(LEDC_CH);
}

void stopOutput() {
  if (output_on) {
    ledcDetachPin(PIN);
    pinMode(PIN, OUTPUT);
    digitalWrite(PIN, LOW);
    output_on = false;
  }
  hr[1] = 0;
}

void applyRegisters() {
  if (hr[2] && hr[0] > 0)
    startOutput(hr[0]);
  else
    stopOutput();
}

// ── Modbus handlers ────────────────────────────────────────────────────────────
ModbusMessage onRead(ModbusMessage req) {
  ModbusMessage rsp;
  uint16_t addr, count;
  req.get(2, addr);
  req.get(4, count);

  if (addr + count > HR_COUNT) {
    rsp.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return rsp;
  }

  rsp.add(req.getServerID(), req.getFunctionCode(), (uint8_t)(count * 2));
  for (uint16_t i = 0; i < count; i++)
    rsp.add(hr[addr + i]);
  return rsp;
}

ModbusMessage onWrite(ModbusMessage req) {
  ModbusMessage rsp;
  uint16_t addr, val;
  req.get(2, addr);
  req.get(4, val);

  if (addr >= HR_COUNT || addr == 1) {
    rsp.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return rsp;
  }
  if ((addr == 0 && val > FREQ_MAX) || (addr == 2 && val > 1)) {
    rsp.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_VALUE);
    return rsp;
  }

  hr[addr] = val;
  hr_dirty = true;
  rsp.add(req.getServerID(), req.getFunctionCode(), addr, val);
  return rsp;
}

// ── Arduino entry points ───────────────────────────────────────────────────────
void setup() {
  Serial.begin(BAUD);

  // Boot with default 5 kHz output
  startOutput(FREQ_DEFAULT);

  mb.registerWorker(MB_ID, READ_HOLD_REGISTER,  &onRead);
  mb.registerWorker(MB_ID, WRITE_HOLD_REGISTER, &onWrite);
  mb.begin(Serial);
}

void loop() {
  if (hr_dirty) {
    hr_dirty = false;
    applyRegisters();
  }
  delay(1);
}
