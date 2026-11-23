# esp32_pm2.5 WiFi 配网 + OTA 升级 实施计划

## 仓库研究结论

### 当前项目现状
- **架构**：裸 `loop()` 轮询，无 FreeRTOS，任务之间靠时间片 `delay(5)` 协作
- **触摸按键**（GPIO10, TTP223, 高电平触发）：短按 → AUTO↔MANUAL_FULL，长按(1s) → MANUAL_FULL↔MANUAL_STOP
- **无 WiFi**：完全离线，`setup()` 里无任何网络初始化
- **无分区表**：使用 PlatformIO 默认分区（仅 1 个 app 分区 factory）
- **库依赖**：LovyanGFX、DHT sensor library、Adafruit Unified Sensor

### 源项目（esp32_screen_st7735）WiFiManager 核心能力
1. **多凭据管理**：最多 5 组 SSID/密码，存 NVS（命名空间 `wifi-config`），新保存的置顶
2. **智能自动连接**：先扫描可见网络，按 RSSI 降序尝试；扫描失败则逐个盲试
3. **Captive Portal 配网**：AP 模式（SSID `esp32-<MAC末两字节>`）+ DNS 劫持 → 手机连热点自动弹配置页，前端 JS 动态扫描/选择/保存
4. **断线退避重连**：5 次 ×10s 失败后等 5 分钟循环，避免射频满载
5. **配网超时禁用**：总窗口 10 分钟，超时后关 WiFi（重启后恢复）
6. **SmartConfig**：支持 ESP-Touch 协议（当前主要用 AP 门户，SmartConfig 保留接口）
7. **联网就绪标志**：`_netReady` 隔离 WiFi 状态和应用层，未连接时阻止联网访问
8. **LittleFS 文件管理**：`/fs` 路由，上传/下载/删除

### OTA 方案选择
| 方案 | 优点 | 缺点 | 选定 |
|------|------|------|------|
| ArduinoOTA | IDE 一键上传 | 需 mDNS，手机不可用 | 否（作为备选） |
| **HTTP Update** | 复用已有 WebServer，手机/PC 浏览器皆可，无需 mDNS | 需自己写上传端点和进度页 | **是** |
| HTTPS + 签名 | 安全 | 证书管理复杂，资源紧张 | 否（局域网够用） |

### 触摸按键冲突解决
当前项目长按(1s) → MANUAL_FULL↔MANUAL_STOP，与源项目长按 → 配网冲突。

**方案：长按 3s 以上进入配网模式**
- 短按 (< 50ms) → AUTO ↔ MANUAL_FULL（不变）
- 长按 0.5~2s → MANUAL_FULL ↔ MANUAL_STOP（原长按逻辑，缩短到 0.5s）
- 长按 > 3s → 进入配网/OTA 门户

这样三个触发互不干扰，用户操作直觉也合理。

---

## 文件和模块变更

### 新增文件
| 文件 | 来源 | 说明 |
|------|------|------|
| `src/WiFiManager.h` | 源项目精简 | 去掉 LittleFS 文件管理、SmartConfig（保留接口），保留核心配网 |
| `src/WiFiManager.cpp` | 源项目精简 | 同上 |
| `src/OtaManager.h` | 新建 | OTA 封装，HTTP Update 进度回调 |
| `src/OtaManager.cpp` | 新建 | `/update` 上传端点 + 进度日志 |
| `partitions.csv` | 新建 | OTA 双分区表 |

### 修改文件
| 文件 | 变更 |
|------|------|
| `platformio.ini` | 加 `board_build.partitions = partitions.csv`；加 `LittleFS.h` 标志；加 `arduinoWebSockets` 不需要，但需 `Update` 内置库 |
| `src/main.cpp` | setup() 加 WiFi 初始化 + OTA 注册；loop() 加 `wifiManager.maintainConnection()` 调度；触摸逻辑改三档时间窗口 |
| `include/config.h` | 加 `WIFI_TX_POWER_RAW 44` |

---

## 实施步骤

### 第 1 步：创建分区表 + platformio.ini 调整（前置）
- 创建 `partitions.csv`：nvs(0x5000) + ota_0(0x1F0000) + ota_1(0x1F0000) + spiffs(0x100000)
- 修改 `platformio.ini`：`board_build.partitions = partitions.csv`

### 第 2 步：精简移植 WiFiManager
- 从源项目拷贝 WiFiManager.h/.cpp
- 删除 LittleFS 文件管理相关（`handleFS`/`handleDownload`/`handleUpload`/`handleDelete` 等）
- `WebServer` 从 WiFiManager 内部独立出来，让 OTA 和配网共用同一个端口
- `config.h` 加 `WIFI_TX_POWER_RAW`
- 保留：多凭据、Captive Portal、DNS 劫持、断线退避、超时禁用、SmartConfig 接口、联网就绪标志

### 第 3 步：创建 OtaManager
- 新建 `OtaManager` 类：封装 `Update` 库
- 在 WiFiManager 的 WebServer 上注册 `/update` POST 端点：接收 `.bin` multipart 上传 → `Update.begin()` → `Update.write()` → `Update.end()`
- 进度日志（百分比 + 速度）
- 更新成功后自动 `ESP.restart()`
- 配门户 `/` 页面加"固件升级"入口

### 第 4 步：集成到 main.cpp
- `setup()`：LittleFS 初始化 → WiFiManager.begin() → WiFiManager.connect()（非阻塞，配网走门户）
- `loop()`：`wifiManager.maintainConnection()` 每次迭代调用
- 触摸逻辑重构为三档窗口
- UI 加 WiFi 状态指示（已连接/AP 配网/离线）

### 第 5 步：配门户页面 OTA 入口
- 修改 `handleRoot()` HTML，底部加"固件升级"按钮
- 点击后显示 `/update` 上传表单（带进度条 JS）
- 或者在 `/` 页面直接嵌入升级区

---

## 依赖和注意事项

1. **OTA 必须先改分区表**：默认单 factory 分区无法 OTA，必须双 app 分区 + 大 SPIFFS
2. **分区偏移计算**：ESP32-C3 4MB flash → 两个 app 分区各 1.9MB 足够（当前编译产物约 ~1MB）
3. **WebServer 端口共享**：OTA 和配网共用 80 端口，配网时不启动 OTA（只有联网后才可用）
4. **内存预算**：WiFiManager + OTA 增加约 30-50KB RAM，当前工程需确认 free heap 够用
5. **编译产物大小**：加 WiFiManager 后可能超过 1MB，OTA 分区要够大
6. **LittleFS**：OTA 方案不需要 LittleFS（不需要文件系统存资源），但分区表里保留以备将来用

---

## 验证

1. **编译通过**：`pio run` 无错误
2. **首次启动无凭据**：自动进入 AP 门户，手机搜到 `esp32-xxxx`
3. **配网成功**：输入 SSID/密码 → 保存 → 重启 → 自动联网
4. **断线重连**：路由器关掉 20s → 恢复 → 设备自动重连
5. **OTA 升级**：联网后访问设备 IP，上传新 bin → 进度条走完 → 自动重启 → 新固件运行
6. **触摸三档**：短按切换 AUTO/MANUAL，长按 1s 切换 FULL/STOP，长按 3s 进配网
7. **配网超时禁用**：10 分钟未配置 → WiFi 关闭 → 重启恢复

---

## 风险

| 风险 | 处理 |
|------|------|
| 分区表偏移错误导致启动失败 | 仔细计算，先烧录测试 app 能否正常运行 |
| WiFiManager 移植时引用源项目不存在的函数 | 已精简，删除 FS 相关 |
| OTA 上传中断 → 设备变砖 | ESP32 OTA 有 rolling back 机制，ota_0 启动失败会回退 ota_1；初始 factory 分区保留作兜底 |
| 触摸时间窗口在机械按键上抖动 | 已有 debounce(50ms)，再加时间窗口阈值（0.5s / 3s），边界处不会误判 |
| RAM 不足 | 编译后关注 free heap，必要时削减 HTML 长度或减少凭据数 |
