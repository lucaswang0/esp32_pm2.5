#include "ui.h"

#include "Display.h"
#include "config.h"

// ===== 配色（RGB565）=====
static constexpr uint16_t C_BG     = 0x0882;  // #0D1117 背景
static constexpr uint16_t C_CARD   = 0x10C4;  // #161B22 卡片
static constexpr uint16_t C_BORDER = 0x2166;  // #262D36 描边
static constexpr uint16_t C_TEXT   = 0xE79E;  // #E6EDF3 主文字
static constexpr uint16_t C_DIM    = 0x7C32;  // #7D8590 次要文字
static constexpr uint16_t C_GREEN  = 0x3DCA;  // #3FB950
static constexpr uint16_t C_YELLOW = 0xD4C4;  // #D29922
static constexpr uint16_t C_RED    = 0xFA89;  // #F85149

static bool s_ok = false;
static bool s_useSprite = false;
static lgfx::LGFX_Sprite s_spr(&tft);

static uint16_t levelColor(const UiData &d) {
  if (!d.pmValid) return C_DIM;
  if (d.pm < PM_OFF_THRESHOLD) return C_GREEN;
  if (d.pm < PM_ON_THRESHOLD) return C_YELLOW;
  return C_RED;
}

static void drawCard(LovyanGFX &g, int x, int y, const char *value, const char *label) {
  g.fillRoundRect(x, y, 40, 38, 6, C_CARD);
  g.drawRoundRect(x, y, 40, 38, 6, C_BORDER);

  g.setFont(&lgfx::fonts::FreeSansBold9pt7b);
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.setTextColor(C_TEXT);
  g.drawString(value, x + 20, y + 14);

  g.setFont(&lgfx::fonts::Font0);
  g.setTextColor(C_DIM);
  g.drawString(label, x + 20, y + 29);
}

static void drawFrame(LovyanGFX &g, const UiData &d) {
  char buf[24];
  uint16_t accent = levelColor(d);

  g.fillScreen(C_BG);

  // ===== 顶部：标题 + 模式胶囊 =====
  g.setFont(&lgfx::fonts::FreeSansBold9pt7b);
  g.setTextDatum(lgfx::textdatum_t::top_left);
  g.setTextColor(C_TEXT);
  g.drawString("PM2.5", 6, 1);

  uint16_t pillBg = C_GREEN;
  if (d.mode[0] == 'M') pillBg = (d.mode[4] == 'F') ? C_YELLOW : C_DIM;
  g.fillRoundRect(83, 2, 42, 13, 6, pillBg);
  g.setFont(&lgfx::fonts::Font0);
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.setTextColor(C_BG);
  g.drawString(d.mode, 104, 9);

  // ===== 主数值 =====
  g.setFont(&lgfx::fonts::FreeSansBold24pt7b);
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  if (d.pmValid) {
    snprintf(buf, sizeof(buf), "%u", d.pm);
    g.setTextColor(accent);
  } else {
    snprintf(buf, sizeof(buf), "---");
    g.setTextColor(C_DIM);
  }
  g.drawString(buf, 64, 35);

  g.setFont(&lgfx::fonts::Font0);
  g.setTextColor(C_DIM);
  g.drawString("ug/m3", 64, 56);

  // ===== 浓度条 =====
  g.fillRoundRect(8, 64, 112, 6, 3, C_CARD);
  if (d.pmValid && d.pm > 0) {
    int v = d.pm > 300 ? 300 : d.pm;
    int w = v * 112 / 300;
    if (w < 6) w = 6;
    g.fillRoundRect(8, 64, w, 6, 3, accent);
  }

  // ===== 三张数据卡 =====
  if (d.envValid) {
    snprintf(buf, sizeof(buf), "%.1fC", d.temp);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  drawCard(g, 2, 74, buf, "TEMP");

  if (d.envValid) {
    snprintf(buf, sizeof(buf), "%.0f%%", d.hum);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  drawCard(g, 44, 74, buf, "HUM");

  snprintf(buf, sizeof(buf), "%u%%", d.duty);
  drawCard(g, 86, 74, buf, "FAN");

  // ===== 底部状态行 =====
  uint16_t dot = d.fault ? C_RED : (d.relayOn ? C_GREEN : C_DIM);
  g.fillCircle(10, 119, 2, dot);

  g.setFont(&lgfx::fonts::Font0);
  g.setTextDatum(lgfx::textdatum_t::middle_left);
  g.setTextColor(d.fault ? C_RED : (d.warmup ? C_YELLOW : C_TEXT));

  const char *state = d.warmup ? "WARMUP"
                      : (!d.pmValid) ? "NO SENSOR"
                      : (d.fault) ? "FAN ERR"
                                  : nullptr;
  if (state) {
    snprintf(buf, sizeof(buf), "%s  %s", d.relayOn ? "12V ON" : "12V OFF", state);
  } else {
    snprintf(buf, sizeof(buf), "%s  %u RPM", d.relayOn ? "12V ON" : "12V OFF", d.rpm);
  }
  g.drawString(buf, 16, 119);
}

void uiBegin() {
  // 硬件复位（与 esp32_screen_st7735 项目一致）
  pinMode(PIN_TFT_RST, OUTPUT);
  digitalWrite(PIN_TFT_RST, LOW);
  delay(100);
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(150);

  tft.init();
  tft.setRotation(TFT_ROTATION);
  s_ok = true;

  // 全屏精灵缓冲：整帧合成后一次推送，避免闪烁
  s_spr.setColorDepth(16);
  s_spr.createSprite(TFT_WIDTH_PX, TFT_HEIGHT_PX);
  s_useSprite = (s_spr.getBuffer() != nullptr);
  if (!s_useSprite) {
    Serial.println("[UI] 精灵缓冲分配失败，退回直接绘制");
  }

  // 背光（LEDC 通道 1，风扇用通道 0）
  ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);
  ledcAttachPin(PIN_TFT_BL, BACKLIGHT_CHANNEL);
  ledcWrite(BACKLIGHT_CHANNEL, (BACKLIGHT_PERCENT * 255 + 50) / 100);

  tft.fillScreen(C_BG);
  Serial.printf("[UI] ST7735S 128x128 初始化完成，精灵缓冲=%s，堆剩余 %lu 字节\n",
                s_useSprite ? "是" : "否", (unsigned long)ESP.getFreeHeap());
}

void uiUpdate(const UiData &d) {
  if (!s_ok) return;

  if (s_useSprite) {
    drawFrame(s_spr, d);
    s_spr.pushSprite(0, 0);
  } else {
    drawFrame(tft, d);
  }
}
