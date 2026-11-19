#pragma once

#include <Arduino.h>

void fanInit();
void fanSetDuty(uint8_t percent);  // 0~100，内部按驱动方式处理反相
uint8_t fanDuty();                 // 最近一次指令占空比
void fanTick();                    // 每 1s 统计一次转速
uint16_t fanRpm();
