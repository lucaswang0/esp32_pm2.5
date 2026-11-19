#include <Arduino.h>
#include <DHT.h>

#include "config.h"
#include "fan.h"
#include "sy210.h"
#include "ui.h"

// ===== 模式与状态 =====
enum Mode : uint8_t { MODE_AUTO, MODE_MANUAL_FULL, MODE_MANUAL_STOP };
enum CtrlState : uint8_t { ST_OFF, ST_ON, ST_STOPPING };

static Mode s_mode = MODE_AUTO;
static CtrlState s_state = ST_OFF;
static bool s_relayOn = false;
static bool s_fault = false;

static Sy210 s_sensor;
static DHT s_dht(PIN_DHT11, DHT11);

// ===== 数据 =====
static uint16_t s_pmHist[PM_SAMPLE_AVG] = {0};
static uint8_t s_pmIdx = 0;
static uint8_t s_pmCount = 0;
static uint16_t s_pmAvg = 0;

static float s_temp = 0;
static float s_hum = 0;
static bool s_envValid = false;

static uint32_t s_bootMs = 0;
static uint32_t s_lastDhtMs = 0;
static uint32_t s_lastLogMs = 0;
static uint32_t s_lastUiMs = 0;
static uint32_t s_lastCtrlMs = 0;
static uint32_t s_onAccum = 0;
static uint32_t s_offAccum = 0;
static uint32_t s_stopMs = 0;
static uint32_t s_faultAccum = 0;

static bool warmedUp(uint32_t now) { return (now - s_bootMs) >= WARMUP_MS; }

static const char *modeText() {
  switch (s_mode) {
    case MODE_MANUAL_FULL: return "MAN-F";
    case MODE_MANUAL_STOP: return "MAN-S";
    default: return "AUTO";
  }
}

static void relayWrite(bool on) {
  if (on == s_relayOn) return;
  digitalWrite(PIN_12V_SW, on ? LOW : HIGH);  // 低电平触发
  s_relayOn = on;
  Serial.printf("[RELAY] 12V %s\n", on ? "ON" : "OFF");
}

static uint8_t dutyForPm(uint16_t pm) {
  if (pm <= PM_ON_THRESHOLD) return DUTY_MIN_PCT;
  if (pm >= DUTY_MAP_HI_PM) return DUTY_MAX_PCT;
  uint32_t span = DUTY_MAX_PCT - DUTY_MIN_PCT;
  return (uint8_t)(DUTY_MIN_PCT + span * (pm - PM_ON_THRESHOLD) / (DUTY_MAP_HI_PM - PM_ON_THRESHOLD));
}

static void resetAutoTimers() {
  s_onAccum = 0;
  s_offAccum = 0;
}

static void dhtTask(uint32_t now) {
  if (now - s_lastDhtMs < 2000) return;
  s_lastDhtMs = now;

  float t = s_dht.readTemperature();
  float h = s_dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    if (s_envValid) Serial.println("[DHT] 读取失败，标记为无效");
    s_envValid = false;
    return;
  }
  s_temp = t;
  s_hum = h;
  s_envValid = true;
}

static void touchTask(uint32_t now) {
  static bool pressed = false;
  static bool longFired = false;
  static uint32_t pressMs = 0;

  bool raw = (digitalRead(PIN_TOUCH) == TOUCH_ACTIVE_LEVEL);
  if (raw == pressed) {
    if (pressed && !longFired && (now - pressMs) >= TOUCH_LONG_MS) {
      longFired = true;
      if (s_mode == MODE_MANUAL_FULL) {
        s_mode = MODE_MANUAL_STOP;
      } else {
        s_mode = MODE_MANUAL_FULL;
      }
      Serial.printf("[TOUCH] 长按 -> %s\n", modeText());
    }
    return;
  }

  pressed = raw;
  if (pressed) {
    pressMs = now;
    longFired = false;
    return;
  }

  if (!longFired && (now - pressMs) >= TOUCH_DEBOUNCE_MS) {
    s_mode = (s_mode == MODE_AUTO) ? MODE_MANUAL_FULL : MODE_AUTO;
    resetAutoTimers();
    Serial.printf("[TOUCH] 短按 -> %s\n", modeText());
  }
}

static void controlTask(uint32_t now) {
  uint32_t dt = now - s_lastCtrlMs;
  if (dt < 50) return;
  s_lastCtrlMs = now;

  // 手动模式优先，且不受预热/传感器失效影响
  if (s_mode == MODE_MANUAL_FULL) {
    relayWrite(true);
    fanSetDuty(100);
    s_state = ST_ON;
    return;
  }
  if (s_mode == MODE_MANUAL_STOP) {
    fanSetDuty(0);
    relayWrite(false);
    s_state = ST_OFF;
    resetAutoTimers();
    return;
  }

  // AUTO：预热期或传感器失效时强制停机
  if (!warmedUp(now) || !s_sensor.valid() || s_pmCount == 0) {
    fanSetDuty(0);
    relayWrite(false);
    s_state = ST_OFF;
    resetAutoTimers();
    return;
  }

  switch (s_state) {
    case ST_OFF:
      if (s_pmAvg >= PM_ON_THRESHOLD) {
        s_onAccum += dt;
        if (s_onAccum >= PM_START_CONFIRM_MS) {
          relayWrite(true);
          s_state = ST_ON;
          s_offAccum = 0;
          s_onAccum = 0;
          Serial.printf("[CTRL] 启动，PM=%.0f -> duty=%u%%\n", (float)s_pmAvg, dutyForPm(s_pmAvg));
        }
      } else {
        s_onAccum = 0;
      }
      break;

    case ST_ON:
      fanSetDuty(dutyForPm(s_pmAvg));
      if (s_pmAvg <= PM_OFF_THRESHOLD) {
        s_offAccum += dt;
        if (s_offAccum >= PM_STOP_HOLD_MS) {
          fanSetDuty(0);
          s_state = ST_STOPPING;
          s_stopMs = now;
          Serial.println("[CTRL] 浓度持续偏低，占空比归零，准备断电");
        }
      } else {
        s_offAccum = 0;
      }
      break;

    case ST_STOPPING:
      if (now - s_stopMs >= RELAY_OFF_DELAY_MS) {
        relayWrite(false);
        s_state = ST_OFF;
        resetAutoTimers();
        Serial.println("[CTRL] 已断电");
      }
      break;
  }
}

static void faultTask(uint32_t now) {
  static uint32_t lastMs = 0;
  uint32_t dt = now - lastMs;
  if (dt < 50) return;
  lastMs = now;

  bool suspect = (s_state == ST_ON) && fanDuty() >= FAN_FAULT_DUTY_PCT && fanRpm() == 0;
  if (suspect) {
    s_faultAccum += dt;
    if (!s_fault && s_faultAccum >= FAN_FAULT_MS) {
      s_fault = true;
      Serial.println("[FAN] 故障：指令占空比高但检测不到转速");
    }
  } else {
    s_faultAccum = 0;
    if (s_fault && fanRpm() > 0) {
      s_fault = false;
      Serial.println("[FAN] 转速已恢复");
    }
  }
}

static void pmTask() {
  if (!s_sensor.poll()) return;

  s_pmHist[s_pmIdx] = s_sensor.pm25();
  s_pmIdx = (uint8_t)((s_pmIdx + 1) % PM_SAMPLE_AVG);
  if (s_pmCount < PM_SAMPLE_AVG) s_pmCount++;

  uint32_t sum = 0;
  for (uint8_t i = 0; i < s_pmCount; i++) sum += s_pmHist[i];
  s_pmAvg = (uint16_t)(sum / s_pmCount);
}

static void uiTask(uint32_t now) {
  if (now - s_lastUiMs < 250) return;
  s_lastUiMs = now;

  UiData d;
  d.pm = s_pmAvg;
  d.pmValid = s_sensor.valid() && s_pmCount > 0;
  d.warmup = !warmedUp(now);
  d.temp = s_temp;
  d.hum = s_hum;
  d.envValid = s_envValid;
  d.duty = fanDuty();
  d.rpm = fanRpm();
  d.relayOn = s_relayOn;
  d.fault = s_fault;
  d.mode = modeText();
  uiUpdate(d);
}

static void logTask(uint32_t now) {
  if (now - s_lastLogMs < 1000) return;
  s_lastLogMs = now;

  Serial.printf("[LOG] PM=%u(avg=%u) T=%.1f H=%.0f duty=%u rpm=%u 12V=%s mode=%s frames=%lu err=%lu%s\n",
                s_sensor.pm25(), s_pmAvg, s_temp, s_hum, fanDuty(), fanRpm(),
                s_relayOn ? "ON" : "OFF", modeText(), (unsigned long)s_sensor.frameCount(),
                (unsigned long)s_sensor.errorCount(), s_fault ? " FAULT" : "");
}

void setup() {
  // 先建立安全电平：GPIO8 上电必须为高，继电器断开
  digitalWrite(PIN_12V_SW, HIGH);
  pinMode(PIN_12V_SW, OUTPUT);

  Serial.begin(115200);
  uint32_t waitMs = millis();
  while (!Serial && (millis() - waitMs) < 2000) {
    delay(10);
  }

  Serial.println();
  Serial.println("=== ESP32-C3 灰尘自动调速风扇 ===");

  pinMode(PIN_TOUCH, INPUT);

  uiBegin();
  fanInit();
  s_sensor.begin();
  s_dht.begin();

  s_bootMs = millis();
  s_lastDhtMs = 0;
  resetAutoTimers();

  Serial.printf("[SYS] 预热 %lus，启动阈值 %d，停止阈值 %d\n", (unsigned long)(WARMUP_MS / 1000),
                PM_ON_THRESHOLD, PM_OFF_THRESHOLD);
}

void loop() {
  uint32_t now = millis();

  pmTask();
  dhtTask(now);
  fanTick();
  touchTask(now);
  controlTask(now);
  faultTask(now);
  uiTask(now);
  logTask(now);

  delay(5);
}
