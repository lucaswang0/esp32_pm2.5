# ESP32-C3 灰尘浓度自动调速风扇

基于 ESP32-C3 Super Mini 的 PM2.5 自动调速风扇：SY210 激光粉尘传感器采集浓度，DHT11 采集温湿度，ST7735S 屏幕显示状态，自动控制 12V 风扇转速并在空气达标后断电。支持 WiFi 配网、自动在线升级（GitHub Pages OTA）。

## 功能

- PM2.5 浓度滑动平均（5 点），超阈值延时确认后启动风扇
- 浓度越高占空比越高（30% ~ 100%），低于阈值并持续一段时间后先停转、再断 12V
- 开机预热 60s，预热期内或传感器数据失效时强制停机
- 触摸按键三档触发（见下方"操作"）
- 风扇堵转检测（占空比高但无转速）并在串口报警
- WiFi 自动连接（多凭据 + RSSI 排序）、Captive Portal 配网、断线退避重连
- **自动 OTA 升级**：开机 30s 后检查 GitHub Pages version.json，每 12 小时再查一次
- **手动 OTA 升级**：长按 3s 进配网门户 → 手机浏览器访问设备 IP → 上传固件
- 串口每秒输出运行状态（PM / T / H / 风扇 / WiFi / 固件版本）

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

## 操作（触摸按键）

| 动作 | 时长 | 效果 |
| --- | --- | --- |
| 短按 | < 300ms | AUTO ↔ 手动全速 |
| 中按 | 500ms ~ 3s | 手动全速 ↔ 手动停机 |
| 长按 | ≥ 3s | 进入配网 / OTA 门户（AP 模式，SSID `esp32-xxxx`） |

进入配网门户后，手机连 `esp32-xxxx` → 打开浏览器自动弹出配置页 → 填写 Wi-Fi SSID/密码 → 保存 → 设备重启联网。

联网后设备 IP 可在路由器或串口日志中查看。访问 `http://<设备IP>/` 可再次进入配网 + 手动 OTA 升级页。

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

### 首次烧录（含分区表变更）

分区表改成了双 OTA 分区（`ota_0` + `ota_1` 各 1.9MB），首次烧录前必须擦除 flash：

```powershell
pio run -t erase
pio run -t upload
```

## 控制参数

主要阈值、PWM 范围、预热时间等集中在 [config.h](include/config.h)：

- `PM_ON_THRESHOLD` / `PM_OFF_THRESHOLD`：启动 / 停止浓度阈值（μg/m³）
- `PM_START_CONFIRM_MS` / `PM_STOP_HOLD_MS`：启动确认与停机保持时间
- `DUTY_MIN_PCT` / `DUTY_MAX_PCT` / `DUTY_MAP_HI_PM`：占空比映射
- `WARMUP_MS`：传感器上电预热时间
- `FIRMWARE_VERSION`：固件版本号（日期格式 YYYY.MM.DD，OTA 自动对比用）
- `OTA_CHECK_URL`：GitHub Pages version.json 地址

## OTA 升级

### 自动（推荐）

设备联网后自动检查 `OTA_CHECK_URL` 指向的 `version.json`，版本号（日期）更晚则下载刷写并重启，无需用户操作。

### 手动

1. 长按触摸键 3s 进入配网门户
2. 手机连 `esp32-xxxx` → 浏览器自动弹出配置页
3. 底部"固件升级"区域选择 `.bin` 文件 → 上传

### 发布新版本

**方式 A：本地 PowerShell 脚本**

```powershell
# 改 config.h 的 FIRMWARE_VERSION 为当天日期，然后：
.\scripts\release_ota.ps1 -AutoPush
```

脚本会自动：编译 → 生成 bin + version.json → push 到 `gh-pages` 分支。

**方式 B：GitHub Actions（打 tag）**

```powershell
git tag v20261124
git push origin v20261124
```

Actions 自动编译 → 部署到 GitHub Pages → 上传 bin 到 Releases。

### version.json 格式（部署在 GitHub Pages 的 firmware/ 目录）

```json
{
  "version": "2026.11.23",
  "url": "https://lucaswang0.github.io/esp32_pm2.5/firmware/pm2_fan_v2026.11.23.bin",
  "size": 1159456,
  "build_time": "2026-11-23 16:28:28"
}
```

⚠️ 发布顺序：先上传 `.bin` 文件 → 等 Pages 部署 → 再更新 `version.json`，否则设备会 404。

## 分区表

[partitions.csv](partitions.csv)：

| 分区 | 偏移 | 大小 | 说明 |
| --- | --- | --- | --- |
| nvs | 0x9000 | 20KB | Wi-Fi 凭据等 |
| coredump | 0xE000 | 8KB | 崩溃信息 |
| ota_0 | 0x10000 | 1.9MB | App slot A |
| ota_1 | 0x200000 | 1.9MB | App slot B |
| spiffs | 0x3F0000 | 64KB | 文件系统 |

Flash 4MB，双 OTA 分区为固件在线升级预留。OTA 失败自动回退旧分区，不会变砖。

## 项目结构

```
include/config.h      引脚、阈值、版本号、OTA URL
partitions.csv        双 OTA 分区表
src/main.cpp          主循环、模式状态机、触摸三档、WiFi/OTA 调度
src/WiFiManager.*     配网：多凭据、Captive Portal、DNS 劫持、断线重连
src/OtaManager.*      OTA：自动升级(HTTP) + 手动升级(multipart 上传)
src/sy210.*           SY210 粉尘传感器驱动
src/fan.*             PWM 调速与转速测量
src/ui.*              状态数据结构与界面刷新
src/Display.*         LovyanGFX 显示封装
scripts/release_ota.ps1  一键编译 + 生成 OTA 固件 + 推 gh-pages
.github/workflows/ota_release.yml  GitHub Actions 自动发布（tag 触发）
firmware/             OTA 产物目录（gh-pages 分支上的发布点）
```
