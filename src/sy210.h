#pragma once

#include <Arduino.h>

// SY210 激光粉尘传感器（UART 模式）
// 帧格式：0x42 0x4D DATA_H DATA_L CHK，5 字节累加和 & 0xFF == 0
class Sy210 {
 public:
  void begin();

  // 非阻塞读取串口，返回 true 表示本次解析出一个新的有效帧
  bool poll();

  bool valid() const;          // 最近一次有效帧是否仍在超时窗口内
  uint16_t pm25() const;       // μg/m³
  uint32_t frameCount() const; // 有效帧累计
  uint32_t errorCount() const; // 校验失败累计

 private:
  bool feed(uint8_t b);

  uint8_t _buf[5] = {0};
  uint8_t _pos = 0;
  uint16_t _pm25 = 0;
  uint32_t _frames = 0;
  uint32_t _errors = 0;
  uint32_t _lastMs = 0;
};
