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

static void drawCard(LovyanGFX &g, int x, int y, int w, const char *value, const char *label, uint16_t color) {
  g.fillRoundRect(x, y, w, 28, 5, C_CARD);
  g.drawRoundRect(x, y, w, 28, 5, C_BORDER);

  g.setFont(&lgfx::fonts::FreeSansBold9pt7b);
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.setTextColor(color);
  g.drawString(value, x + w / 2, y + 11);

  g.setFont(&lgfx::fonts::Font0);
  g.setTextColor(C_DIM);
  g.drawString(label, x + w / 2, y + 23);
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

  // ===== 四张数据卡（2×2 布局，占满下半屏 y=64-128）=====
  // 62px 宽 × 28px 高，列间隙 2px；行 1 起点 y=66，行 2 起点 y=98
  // 总高：28 + 4 + 28 = 60，从 y=66 到 y=126，顶部留出 2px 间隙给浓度条
  if (d.envValid) {
    snprintf(buf, sizeof(buf), "%.1fC", d.temp);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  drawCard(g, 2, 66, 62, buf, "TEMP", C_TEXT);

  if (d.envValid) {
    snprintf(buf, sizeof(buf), "%.0f%%", d.hum);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  drawCard(g, 64, 66, 62, buf, "HUM", C_TEXT);

  snprintf(buf, sizeof(buf), "%u%%", d.duty);
  drawCard(g, 2, 98, 62, buf, "DUTY", d.relayOn ? C_GREEN : C_DIM);

  if (d.relayOn) {
    snprintf(buf, sizeof(buf), "%u", d.rpm);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  uint16_t rpmColor = C_DIM;
  if (d.relayOn) {
    rpmColor = d.fault ? C_RED : ((d.rpm < 500) ? C_YELLOW : C_GREEN);
  }
  drawCard(g, 64, 98, 62, buf, "RPM", rpmColor);

  // 12V 指示圆点（左下角）
  uint16_t dot = d.fault ? C_RED : (d.relayOn ? C_GREEN : C_DIM);
  g.fillCircle(8, 122, 2, dot);

  // 异常/未就绪提示：屏幕中央闪烁（约 1Hz），正常时隐藏
  const char *alert = d.warmup ? "WARMUP"
                      : (!d.pmValid) ? "NO SENSOR"
                      : (d.fault) ? "FAN ERR"
                                  : nullptr;
  if (alert && ((millis() / 500) & 1)) {
    uint16_t alertColor = d.fault ? C_RED : C_YELLOW;
    g.setFont(&lgfx::fonts::FreeSansBold9pt7b);
    int bh = 20;
    int bw = g.textWidth(alert) + 18;
    if (bw > TFT_WIDTH_PX - 8) bw = TFT_WIDTH_PX - 8;
    int bx = (TFT_WIDTH_PX - bw) / 2;
    int by = (TFT_HEIGHT_PX - bh) / 2;
    g.fillRoundRect(bx, by, bw, bh, 8, C_CARD);
    g.drawRoundRect(bx, by, bw, bh, 8, alertColor);
    g.setTextDatum(lgfx::textdatum_t::middle_center);
    g.setTextColor(alertColor);
    g.drawString(alert, TFT_WIDTH_PX / 2, TFT_HEIGHT_PX / 2);
  }
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
