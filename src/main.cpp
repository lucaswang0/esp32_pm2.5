#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>

#include "config.h"
#include "fan.h"
#include "sy210.h"
#include "ui.h"
#include "WiFiManager.h"
#include "OtaManager.h"

// ===== 模式与状态 =====
enum Mode : uint8_t { MODE_AUTO, MODE_MANUAL_FULL, MODE_MANUAL_STOP };
enum CtrlState : uint8_t { ST_OFF, ST_ON, ST_STOPPING };

static Mode s_mode = MODE_AUTO;
static CtrlState s_state = ST_OFF;
static bool s_relayOn = false;
static bool s_fault = false;

static Sy210 s_sensor;
static DHT s_dht(PIN_DHT11, DHT11);

// ===== WiFi & OTA =====
static WiFiManager wifiManager;
static OtaManager otaManager;
static bool s_wifiInitDone = false;

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
  digitalWrite(PIN_12V_SW, on ? LOW : HIGH);
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

// ===== 触摸按键（三档时间窗口） =====
// < 300ms  短按 → AUTO↔MANUAL_FULL
// 500~3000ms 中按 → MANUAL_FULL↔MANUAL_STOP
// >= 3000ms 长按 → 进入配网/OTA 门户
static void touchTask(uint32_t now) {
  static bool pressed = false;
  static bool longFired = false;
  static uint32_t pressMs = 0;

  bool raw = (digitalRead(PIN_TOUCH) == TOUCH_ACTIVE_LEVEL);

  // 按下沿
  if (raw && !pressed) {
    pressed = true;
    pressMs = now;
    longFired = false;
    return;
  }

  // 长按触发（按住 >= 3s 立即进入配网门户）
  if (pressed && !longFired && (now - pressMs) >= TOUCH_LONG_MS) {
    longFired = true;
    Serial.println("[TOUCH] 长按 -> 进入配网模式");
    wifiManager.startAPMode();
    return;
  }

  // 抬起沿
  if (!raw && pressed) {
    pressed = false;
    unsigned long dur = now - pressMs;

    if (longFired) return;  // 长按已触发过，忽略抬起

    if (dur < TOUCH_DEBOUNCE_MS) return;  // 抖动忽略

    if (dur < TOUCH_SHORT_MAX_MS) {
      // 短按
      s_mode = (s_mode == MODE_AUTO) ? MODE_MANUAL_FULL : MODE_AUTO;
      resetAutoTimers();
      Serial.printf("[TOUCH] 短按 -> %s\n", modeText());
    } else if (dur >= TOUCH_MID_MIN_MS) {
      // 中按
      if (s_mode == MODE_MANUAL_FULL) {
        s_mode = MODE_MANUAL_STOP;
      } else {
        s_mode = MODE_MANUAL_FULL;
      }
      Serial.printf("[TOUCH] 中按 -> %s\n", modeText());
    }
    // 300ms ~ 500ms 留空隙不触发，避免边界误判
  }
}

static void controlTask(uint32_t now) {
  uint32_t dt = now - s_lastCtrlMs;
  if (dt < 50) return;
  s_lastCtrlMs = now;

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

  Serial.printf("[LOG] PM=%u(avg=%u) T=%.1f H=%.0f duty=%u rpm=%u 12V=%s mode=%s",
                s_sensor.pm25(), s_pmAvg, s_temp, s_hum, fanDuty(), fanRpm(),
                s_relayOn ? "ON" : "OFF", modeText());

  if (wifiManager.isConnected()) {
    Serial.printf(" WiFi=%s RSSI=%d OTA=%s",
                  wifiManager.getLocalIP(), wifiManager.getRSSI(), FIRMWARE_VERSION);
  } else if (wifiManager.isAPStarted()) {
    Serial.printf(" AP=%s", wifiManager.getConfigSSID());
  }

  Serial.printf(" frames=%lu err=%lu%s\n",
                (unsigned long)s_sensor.frameCount(),
                (unsigned long)s_sensor.errorCount(), s_fault ? " FAULT" : "");
}

void setup() {
  digitalWrite(PIN_12V_SW, HIGH);
  pinMode(PIN_12V_SW, OUTPUT);

  Serial.begin(115200);
  uint32_t waitMs = millis();
  while (!Serial && (millis() - waitMs) < 2000) {
    delay(10);
  }

  Serial.println();
  Serial.println("=== ESP32-C3 灰尘自动调速风扇 ===");
  Serial.printf("=== 固件版本: %s ===\n", FIRMWARE_VERSION);

  pinMode(PIN_TOUCH, INPUT);

  uiBegin();
  fanInit();
  s_sensor.begin();
  s_dht.begin();

  // ===== WiFi + OTA 初始化 =====
  Serial.println("[SYS] WiFi 初始化...");
  wifiManager.begin();
  wifiManager.connect();  // 尝试自动连接，失败会进入配网门户

  // OTA 注册路由（注册在 WiFiManager 的 WebServer 上）
  otaManager.begin(wifiManager.getWebServer());

  s_wifiInitDone = true;

  s_bootMs = millis();
  s_lastDhtMs = 0;
  resetAutoTimers();

  Serial.printf("[SYS] 预热 %lus，启动阈值 %d，停止阈值 %d\n", (unsigned long)(WARMUP_MS / 1000),
                PM_ON_THRESHOLD, PM_OFF_THRESHOLD);
  Serial.printf("[SYS] WiFi=%s IP=%s\n",
                wifiManager.isConnected() ? "已连接" : "未连接",
                wifiManager.isConnected() ? wifiManager.getLocalIP() : (wifiManager.isAPStarted() ? wifiManager.getConfigSSID() : "-"));
}

void loop() {
  uint32_t now = millis();

  // WiFi 维护 + OTA 定时检查（必须每次 loop 都调，Captive Portal DNS/WebServer 需要持续处理）
  if (s_wifiInitDone) {
    wifiManager.maintainConnection();
    otaManager.maintain();
  }

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
