# ESP32-C3 灰尘浓度自动调速风扇

基于 ESP32-C3 Super Mini 的 PM2.5 自动调速风扇：SY210 激光粉尘传感器采集浓度，DHT11 采集温湿度，ST7735S 屏幕显示状态，自动控制 12V 风扇转速并在空气达标后断电。

## 功能

- PM2.5 浓度滑动平均（5 点），超阈值延时确认后启动风扇
- 浓度越高占空比越高（30% ~ 100%），低于阈值并持续一段时间后先停转、再断 12V
- 开机预热 60s，预热期内或传感器数据失效时强制停机
- 触摸按键切换模式：短按 AUTO / 手动全速，长按 手动全速 / 手动停机
- 风扇堵转检测（占空比高但无转速）并在串口报警
- 串口每秒输出一次运行状态

## 硬件接线

引脚定义见 [config.h](include/config.h)。

| 功能 | GPIO | 说明 |
| --- | --- | --- |
| TFT SCLK / MOSI / CS / DC / RST / BL | 4 / 3 / 0 / 1 / 2 / 5 | ST7735S 128x128，SPI 40MHz |
| SY210 UART RX | 6 | 9600 8N1，帧头 `0x42 0x4D` |
| DHT11 数据 | 7 | 单总线 |
| 触摸输入 | 10 | TTP223 类模块，高电平有效 |
| 12V 继电器 | 8 | 低电平触发 |
| 风扇 PWM | 20 | 经 NPN/MOS 低边驱动，逻辑反相 |
| 风扇测速 | 21 | 开漏输出，需外部上拉到 3.3V |

> GPIO20/21 默认是 UART0，因此调试输出改走 USB CDC（`ARDUINO_USB_CDC_ON_BOOT=1`）。

## 构建与烧录

依赖：PlatformIO Core 或 VS Code PlatformIO IDE。

本机 C 盘空间不足时，构建产物和库目录指向 D 盘（用环境变量，不写死路径）：

```powershell
$env:PLATFORMIO_CORE_DIR    = "D:\pio_core"
$env:PLATFORMIO_BUILD_DIR   = "D:\pio_build\esp32_pm2.5\build"
$env:PLATFORMIO_LIBDEPS_DIR = "D:\pio_build\esp32_pm2.5\libdeps"
pio run -t upload
```

串口监视：

```powershell
pio device monitor
```

默认端口 `COM4`、波特率 115200，可在 [platformio.ini](platformio.ini) 中修改。

## 控制参数

主要阈值、PWM 范围、预热时间等集中在 [config.h](include/config.h)：

- `PM_ON_THRESHOLD` / `PM_OFF_THRESHOLD`：启动 / 停止浓度阈值（μg/m³）
- `PM_START_CONFIRM_MS` / `PM_STOP_HOLD_MS`：启动确认与停机保持时间
- `DUTY_MIN_PCT` / `DUTY_MAX_PCT` / `DUTY_MAP_HI_PM`：占空比映射
- `WARMUP_MS`：传感器上电预热时间

## 项目结构

```
include/config.h   引脚与参数
src/main.cpp       主循环、模式与控制状态机
src/sy210.*        SY210 粉尘传感器驱动
src/fan.*          PWM 调速与转速测量
src/ui.*           状态数据结构与界面刷新
src/Display.*      LovyanGFX 显示封装
```
