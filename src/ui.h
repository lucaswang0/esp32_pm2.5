#pragma once

#include <Arduino.h>

struct UiData {
  uint16_t pm;       // μg/m³
  bool pmValid;
  bool warmup;
  float temp;
  float hum;
  bool envValid;
  uint8_t duty;      // %
  uint16_t rpm;
  bool relayOn;
  bool fault;
  const char *mode;  // "AUTO" / "MAN-F" / "MAN-S"
};

void uiBegin();
void uiUpdate(const UiData &d);
