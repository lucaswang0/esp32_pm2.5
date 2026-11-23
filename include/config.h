#pragma once

// ===== ST7735S 128x128（LovyanGFX）=====
#define PIN_TFT_SCLK 4
#define PIN_TFT_MOSI 3
#define PIN_TFT_CS   0
#define PIN_TFT_DC   1
#define PIN_TFT_RST  2
#define PIN_TFT_BL   5

#define TFT_WIDTH_PX   128
#define TFT_HEIGHT_PX  128
#define TFT_MEM_WIDTH  128   // ST7735S 控制器 GRAM 尺寸
#define TFT_MEM_HEIGHT 160
// 显存偏移：画面整体偏移/边缘残缺时改为 2 / 1（或 0 / 0）再试
#define TFT_COL_OFFSET 0
#define TFT_ROW_OFFSET 0
#define TFT_ROTATION   2
#define TFT_INVERT     0     // 颜色反相时改为 1
#define TFT_SPI_HZ     40000000

// 背光（LEDC 通道 1，风扇占用通道 0）
#define BACKLIGHT_CHANNEL 1
#define BACKLIGHT_PERCENT 80

// ===== 传感器 =====
#define PIN_SY210_RX 6
#define PIN_DHT11    7
#define PIN_TOUCH    10

#define SY210_BAUD        9600
#define SY210_TIMEOUT_MS  3000   // 超过该时间无有效帧视为数据失效
#define SY210_DEBUG_RAW   0      // 1 = 打印每帧原始字节

// 触摸开关（TTP223 类模块，输出数字电平，三档时间窗口）
// < 300ms 短按 → AUTO↔MANUAL_FULL
// 500~3000ms 中按 → MANUAL_FULL↔MANUAL_STOP
// >= 3000ms 长按 → 进入配网/OTA 门户
#define TOUCH_ACTIVE_LEVEL 1
#define TOUCH_DEBOUNCE_MS    50
#define TOUCH_SHORT_MAX_MS   300
#define TOUCH_MID_MIN_MS     500
#define TOUCH_LONG_MS        3000

// ===== WiFi =====
#define WIFI_TX_POWER_RAW 44   // 11.0 dBm

// ===== OTA =====
#define FIRMWARE_VERSION  "2026.11.23"
// GitHub Pages version.json
#define OTA_CHECK_URL     "https://lucaswang0.github.io/esp32_pm2.5/firmware/version.json"

// ===== 执行器 =====
#define PIN_12V_SW   8    // 低电平触发继电器：LOW=12V 通，HIGH=断
#define PIN_FAN_PWM  20   // 经 NPN/MOS 低边驱动，逻辑反相
#define PIN_FAN_TACH 21   // 开漏输出，外部上拉到 3.3V

#define FAN_PWM_CHANNEL 0
#define FAN_PWM_FREQ    25000
#define FAN_PWM_RES     10
#define FAN_PWM_INVERT  1     // 1 = NPN 反相驱动；0 = 直连/非反相驱动
#define FAN_PULSE_PER_REV 2   // 4 线风扇典型 2 脉冲/转

// ===== 控制参数 =====
#define PM_ON_THRESHOLD      75    // μg/m³，超过则启动
#define PM_OFF_THRESHOLD     35    // μg/m³，低于该值开始计时停机
#define PM_START_CONFIRM_MS  3000  // 超过启动阈值需连续保持时间
#define PM_STOP_HOLD_MS      60000 // 低于停止阈值持续多久才断电
#define PM_SAMPLE_AVG        5     // 滑动平均点数

#define DUTY_MIN_PCT   30    // 4 线风扇低于约 30% 会停转
#define DUTY_MAX_PCT   100
#define DUTY_MAP_HI_PM 300   // 达到该浓度输出 100%

#define WARMUP_MS             60000 // SY210 上电预热
#define RELAY_OFF_DELAY_MS    3000  // 占空比归零后延时再断 12V

#define FAN_FAULT_DUTY_PCT  50    // 指令占空比达到该值
#define FAN_FAULT_MS        10000 // 且持续无转速，判定故障
