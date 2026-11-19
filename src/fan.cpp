#include "fan.h"

#include "config.h"

static volatile uint32_t s_pulses = 0;
static uint16_t s_rpm = 0;
static uint8_t s_duty = 0;
static uint32_t s_lastTickMs = 0;

static void IRAM_ATTR tachIsr() { s_pulses++; }

static uint32_t pwmMax() { return (1UL << FAN_PWM_RES) - 1; }

void fanInit() {
  pinMode(PIN_FAN_TACH, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_FAN_TACH), tachIsr, FALLING);

  ledcSetup(FAN_PWM_CHANNEL, FAN_PWM_FREQ, FAN_PWM_RES);
  ledcAttachPin(PIN_FAN_PWM, FAN_PWM_CHANNEL);

  fanSetDuty(0);
  s_lastTickMs = millis();

  Serial.printf("[FAN] PWM %d Hz / %d bit, GPIO%d, TACH GPIO%d, 反相=%d\n",
                FAN_PWM_FREQ, FAN_PWM_RES, PIN_FAN_PWM, PIN_FAN_TACH, FAN_PWM_INVERT);
}

void fanSetDuty(uint8_t percent) {
  if (percent > 100) percent = 100;
  s_duty = percent;

  uint32_t maxd = pwmMax();
  uint32_t level = maxd * percent / 100;
#if FAN_PWM_INVERT
  // NPN 低边驱动：MCU 占空比 100% 对应风扇 PWM 线持续拉低 = 风扇停
  level = maxd - level;
#endif
  ledcWrite(FAN_PWM_CHANNEL, level);
}

uint8_t fanDuty() { return s_duty; }

uint16_t fanRpm() { return s_rpm; }

void fanTick() {
  uint32_t now = millis();
  uint32_t dt = now - s_lastTickMs;
  if (dt < 1000) return;

  noInterrupts();
  uint32_t pulses = s_pulses;
  s_pulses = 0;
  interrupts();

  // RPM = 脉冲数 / 每转脉冲数 / 时间
  s_rpm = (uint16_t)((uint64_t)pulses * 60000ULL / (dt * FAN_PULSE_PER_REV));
  s_lastTickMs = now;
}
