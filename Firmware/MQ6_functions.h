#ifndef MQ6_FUNCTIONS_H
#define MQ6_FUNCTIONS_H
#pragma once

#include <Arduino.h>
#include "Config.h"
#include "GSM_commands.h"

constexpr uint8_t MQ6_ADC_SAMPLES = 10;
constexpr unsigned long MQ6_SAMPLE_INTERVAL = 100;
constexpr unsigned long MQ6_WARMUP_TIME = 30000UL;  // 30 seconds

constexpr int MQ6_LEAK_ON_THRESHOLD  = 2200;
constexpr int MQ6_LEAK_OFF_THRESHOLD = 2000;

// Gas must remain above ON threshold before declaring leak.
constexpr unsigned long MQ6_LEAK_CONFIRM_TIME = 1000UL;
// Gas must remain below OFF threshold before clearing leak.
constexpr unsigned long MQ6_CLEAR_CONFIRM_TIME = 3000UL;

constexpr unsigned long MQ6_LED_BLINK_INTERVAL = 500UL;

// Sensor fault limits
constexpr int MQ6_SENSOR_TOO_LOW  = 20;
constexpr int MQ6_SENSOR_TOO_HIGH = 4090;

// MQ-6 state
enum MQ6State
{
    MQ6_WARMING_UP,
    MQ6_NORMAL,
    MQ6_LEAK,
    MQ6_SENSOR_FAULT
};

extern MQ6State mq6State;

// Timing

extern unsigned long mq6StartupTime;
extern unsigned long mq6LastSampleTime;

extern unsigned long mq6LeakStartTime;
extern unsigned long mq6ClearStartTime;

extern unsigned long mq6LastLEDBlink;

// ADC data
extern int mq6AverageRaw;
extern int mq6PreviousRaw;

// Status
extern bool gasLeakDetected;
extern bool mq6SensorFault;
extern bool mq6AlarmLEDState;

void initializeMQ6();
int readMQ6Average();
void activateAlarm();
void deactivateAlarm();
void handleMQ6Warmup();
bool isMQ6ReadingValid(int);
void checkMQ6();

#endif