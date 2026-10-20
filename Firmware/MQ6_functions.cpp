#include <Arduino.h>
#include "Config.h"
#include "GSM_commands.h"
#include "MQ6_functions.h"

MQ6State mq6State = MQ6_WARMING_UP;

// Timing

unsigned long mq6StartupTime = 0;
unsigned long mq6LastSampleTime = 0;

unsigned long mq6LeakStartTime = 0;
unsigned long mq6ClearStartTime = 0;

unsigned long mq6LastLEDBlink = 0;

// ADC data
int mq6AverageRaw = 0;
int mq6PreviousRaw = 0;

// Status
bool gasLeakDetected = false;
bool mq6SensorFault = false;
bool mq6AlarmLEDState = false;

// ---------------------------------------- Initialize MQ06 -----------------------------------------------------
void initializeMQ6()
{
    pinMode(MQ06, INPUT);

    // GPIO34 is ADC1_CH6 on the classic ESP32.
    // 11 dB attenuation gives the ADC its highest input range.
    analogSetPinAttenuation(MQ06, ADC_11db);

    // Take initial reading
    mq6AverageRaw = analogRead(MQ06);
    mq6PreviousRaw = mq6AverageRaw;

    mq6StartupTime = millis();
    mq6LastSampleTime = millis();

    mq6State = MQ6_WARMING_UP;
    gasLeakDetected = false;
    mq6SensorFault = false;

    Serial.println();
    Serial.println("================================");
    Serial.println("MQ-6 INITIALIZATION");
    Serial.println("Warm-up time: 30 seconds");
    Serial.println("================================");
}

// ------------------------------------------------- Average 10 samples ----------------------------------------------
int readMQ6Average()
{
    long total = 0;

    for (uint8_t i = 0; i < MQ6_ADC_SAMPLES; i++)
    {
        total += analogRead(MQ06);
    }

    return total / MQ6_ADC_SAMPLES;
}

// --------------------------------------------------------- Alarm -----------------------------------------------------
void activateAlarm()
{
    gasLeakDetected = true;

    digitalWrite(BUZZER, HIGH);

    // Don't immediately force LED state here.
    // The LED will be handled by updateMQ6AlarmLED().
}


void deactivateAlarm()
{
    gasLeakDetected = false;

    digitalWrite(BUZZER, LOW);

    digitalWrite(LED_LEAK, LOW);

    mq6AlarmLEDState = false;
    mq6LastLEDBlink = millis();

    digitalWrite(LED_NORMAL, HIGH);
}

// ----------------------------------------------- MQ06 Warm Up --------------------------------------------------------------
void handleMQ6Warmup()
{
    unsigned long elapsed = millis() - mq6StartupTime;

    if (elapsed < MQ6_WARMUP_TIME)
    {
        mq6State = MQ6_WARMING_UP;

        digitalWrite(BUZZER, LOW);
        digitalWrite(LED_LEAK, LOW);

        // Optional indication:
        digitalWrite(LED_NORMAL, LOW);

        static unsigned long lastPrint = 0;

        if (millis() - lastPrint >= 1000)
        {
            lastPrint = millis();

            unsigned long remaining =
                (MQ6_WARMUP_TIME - elapsed) / 1000;

            Serial.print("MQ-6 warming up... ");
            Serial.print(remaining);
            Serial.println(" seconds remaining");
        }

        return;
    }

    // Warm-up completed
    mq6State = MQ6_NORMAL;

    digitalWrite(LED_NORMAL, HIGH);

    Serial.println("MQ-6 warm-up completed.");
    Serial.println("MQ-6 monitoring started.");
}

// --------------------------------------------- Fault Detection -------------------------------------------
bool isMQ6ReadingValid(int rawValue)
{
    if (rawValue <= MQ6_SENSOR_TOO_LOW)
    {
        return false;
    }

    if (rawValue >= MQ6_SENSOR_TOO_HIGH)
    {
        return false;
    }

    return true;
}

// ----------------------------------------------------------------------------------------------------------------------
void checkMQ6()
{
    unsigned long now = millis();

    // -----------------------------------------------------
    // Handle warm-up
    // -----------------------------------------------------

    if (mq6State == MQ6_WARMING_UP)
    {
        handleMQ6Warmup();

        // Still take measurements during warm-up for debugging.
        if (now - mq6LastSampleTime < MQ6_SAMPLE_INTERVAL)
            return;

        mq6LastSampleTime = now;

        mq6AverageRaw = readMQ6Average();

        Serial.print("MQ-6 warm-up ADC: ");
        Serial.println(mq6AverageRaw);

        return;
    }

    // -----------------------------------------------------
    // Sampling interval
    // -----------------------------------------------------

    if (now - mq6LastSampleTime < MQ6_SAMPLE_INTERVAL)
        return;

    mq6LastSampleTime = now;

    // -----------------------------------------------------
    // Take 10 samples and average them
    // -----------------------------------------------------

    int averageRaw = readMQ6Average();

    mq6PreviousRaw = mq6AverageRaw;
    mq6AverageRaw = averageRaw;

    Serial.print("MQ-6 ADC average: ");
    Serial.println(mq6AverageRaw);

    // -----------------------------------------------------
    // Sensor fault detection
    // -----------------------------------------------------

    if (!isMQ6ReadingValid(mq6AverageRaw))
    {
        if (!mq6SensorFault)
        {
            mq6SensorFault = true;
            mq6State = MQ6_SENSOR_FAULT;

            Serial.println("ERROR: MQ-6 sensor reading invalid.");

            digitalWrite(BUZZER, LOW);
            digitalWrite(LED_LEAK, LOW);
            digitalWrite(LED_NORMAL, LOW);
        }

        return;
    }

    // Sensor has recovered
    if (mq6SensorFault)
    {
        mq6SensorFault = false;
        mq6State = MQ6_NORMAL;

        Serial.println("MQ-6 sensor reading recovered.");

        digitalWrite(LED_NORMAL, HIGH);
    }

    // -----------------------------------------------------
    // Leak detection
    // -----------------------------------------------------

    if (!gasLeakDetected)
    {
        // -------------------------------------------------
        // NORMAL → POSSIBLE LEAK
        // -------------------------------------------------

        if (mq6AverageRaw >= MQ6_LEAK_ON_THRESHOLD)
        {
            if (mq6LeakStartTime == 0)
            {
                mq6LeakStartTime = now;

                Serial.println(
                    "MQ-6 above leak threshold. Confirming..."
                );
            }

            // Has the condition remained long enough?
            if (now - mq6LeakStartTime >= MQ6_LEAK_CONFIRM_TIME)
            {
                gasLeakDetected = true;

                mq6State = MQ6_LEAK;

                mq6ClearStartTime = 0;
                mq6LeakStartTime = 0;

                Serial.println();
                Serial.println("==============================");
                Serial.println("WARNING: LPG GAS LEAK DETECTED!");
                Serial.println("==============================");

                digitalWrite(LED_NORMAL, LOW);

                activateAlarm();

                // Send ONE alert when leak begins.
                sendMultipleSMS(
                    "GasGuard ALERT!\n"
                    "LPG gas leak detected.\n"
                    "Please check the gas cylinder and valve immediately."
                );
            }
        }
        else
        {
            // Reading returned below ON threshold
            mq6LeakStartTime = 0;

            mq6State = MQ6_NORMAL;

            digitalWrite(LED_NORMAL, HIGH);
        }
    }

    else
    {
        // -------------------------------------------------
        // LEAK → POSSIBLE CLEAR
        // -------------------------------------------------

        if (mq6AverageRaw <= MQ6_LEAK_OFF_THRESHOLD)
        {
            if (mq6ClearStartTime == 0)
            {
                mq6ClearStartTime = now;

                Serial.println(
                    "MQ-6 below clear threshold. Confirming clear..."
                );
            }

            if (now - mq6ClearStartTime >= MQ6_CLEAR_CONFIRM_TIME)
            {
                Serial.println("Gas leak cleared.");

                mq6ClearStartTime = 0;
                mq6LeakStartTime = 0;

                mq6State = MQ6_NORMAL;

                deactivateAlarm();
            }
        }
        else
        {
            // Gas still above clear threshold
            mq6ClearStartTime = 0;

            mq6State = MQ6_LEAK;
        }
    }
}