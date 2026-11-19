#include "sy210.h"

#include "config.h"

void Sy210::begin() {
  // 传感器只发不收，TX 引脚不分配
  Serial1.begin(SY210_BAUD, SERIAL_8N1, PIN_SY210_RX, -1);
  _pos = 0;
  _frames = 0;
  _errors = 0;
  _lastMs = 0;
  Serial.printf("[SY210] UART1 启动: %d bps, RX=GPIO%d\n", SY210_BAUD, PIN_SY210_RX);
}

bool Sy210::valid() const {
  return _frames > 0 && (millis() - _lastMs) < SY210_TIMEOUT_MS;
}

uint16_t Sy210::pm25() const { return _pm25; }
uint32_t Sy210::frameCount() const { return _frames; }
uint32_t Sy210::errorCount() const { return _errors; }

bool Sy210::poll() {
  bool gotFrame = false;
  while (Serial1.available() > 0) {
    if (feed(Serial1.read())) {
      gotFrame = true;
    }
  }
  return gotFrame;
}

bool Sy210::feed(uint8_t b) {
  if (_pos == 0) {
    if (b == 0x42) _buf[_pos++] = b;
    return false;
  }

  if (_pos == 1) {
    if (b == 0x4D) {
      _buf[_pos++] = b;
    } else if (b == 0x42) {
      _buf[0] = 0x42;  // 重新同步
      _pos = 1;
    } else {
      _pos = 0;
    }
    return false;
  }

  _buf[_pos++] = b;
  if (_pos < sizeof(_buf)) return false;
  _pos = 0;

#if SY210_DEBUG_RAW
  Serial.printf("[SY210] RAW %02X %02X %02X %02X %02X\n", _buf[0], _buf[1], _buf[2], _buf[3], _buf[4]);
#endif

  uint8_t sum = 0;
  for (uint8_t i = 0; i < sizeof(_buf); i++) sum += _buf[i];
  if (sum != 0) {
    _errors++;
    return false;
  }

  uint16_t value = ((uint16_t)_buf[2] << 8) | _buf[3];
  if (value == 0 || value > 4000) {
    // 预热期或超量程，视为无效数据
    return false;
  }

  _pm25 = value;
  _frames++;
  _lastMs = millis();
  return true;
}
