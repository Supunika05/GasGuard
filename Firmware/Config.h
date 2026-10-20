#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ---------- GPIO ----------

constexpr uint8_t BUZZER = 4;
constexpr uint8_t LED_NORMAL = 5;
constexpr uint8_t LED_GASLOW = 18;
constexpr uint8_t LED_LEAK = 19;
constexpr uint8_t MQ06 = 34;

constexpr uint8_t GSM_TX = 16;
constexpr uint8_t GSM_RX = 17;

constexpr uint8_t HX_DT = 32;
constexpr uint8_t HX_SCK = 33;

// ---------- Cylinder configuration ----------

constexpr uint8_t CYLINDER_TYPE_COUNT = 4;

// ---------- Gas-level measurement ----------

// Number of HX711 readings used for one gas-level measurement.
constexpr uint8_t GAS_WEIGHT_SAMPLES = 10;

// Maximum physically accepted measured weight.
constexpr float MAX_VALID_WEIGHT_KG = 30.0f;

// Small negative values caused by load-cell noise are tolerated.
constexpr float MIN_VALID_WEIGHT_KG = -0.20f;

constexpr float GAS_LOW_THRESHOLD_PERCENT = 20.0f;
constexpr float GAS_LOW_RECOVERY_PERCENT = 30.0f;

// Maximum time allowed for obtaining a gas measurement.
constexpr unsigned long GAS_MEASUREMENT_TIMEOUT_MS = 2000;

// ---------- First-startup tare ----------
constexpr unsigned long TARE_TIMEOUT_MS = 5000;

// ---------- Daily gas-level check ----------

constexpr uint8_t DAILY_CHECK_HOUR = 10;

// RTC-based daily check is allowed during the first 5 minutes after 10:00
constexpr uint8_t DAILY_CHECK_WINDOW_MINUTES = 5;

// Fallback when RTC/network time is unavailable.
constexpr unsigned long DAILY_FALLBACK_INTERVAL_MS =
    24UL * 60UL * 60UL * 1000UL;

// ---------- SMS ----------

constexpr uint8_t SMS_MAX_ATTEMPTS = 3;
constexpr unsigned long SMS_RETRY_DELAY_MS = 2000;

#endif