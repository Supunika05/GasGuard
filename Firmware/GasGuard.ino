#include <HardwareSerial.h>

#define TINY_GSM_MODEM_SIM800

#include <TinyGsmClient.h>
#include <time.h>
#include <Preferences.h>

#include "HX711.h"
#include "Config.h"
#include "GSM_commands.h"
#include "MQ6_functions.h"
#include "GasLevel.h"

// ------------------------------------------------------------------
// Hardware objects
// ------------------------------------------------------------------

HardwareSerial SIM800L(2);

TinyGsm modem(SIM800L);

HX711 scale;

Preferences preferences;

// ------------------------------------------------------------------
// Timing
// ------------------------------------------------------------------

unsigned long previousMQ6Check = 0;

const unsigned long MQ6_INTERVAL = 500UL;

// ------------------------------------------------------------------
// Daily scheduling
// ------------------------------------------------------------------

static int lastDailySMSYear = -1;
static int lastDailySMSDay = -1;

static unsigned long fallbackDailyStartTime = 0;

static bool rtcAvailable = false;

// ------------------------------------------------------------------
// Gas-level operation state
// ------------------------------------------------------------------

static bool gasMeasurementInProgress = false;

// ------------------------------------------------------------------
// Load daily state from Preferences
// ------------------------------------------------------------------

static void loadDailyState()
{
    preferences.begin(
        "GasGuard",
        true
    );

    lastDailySMSYear =
        preferences.getInt(
            "daily_year",
            -1
        );

    lastDailySMSDay =
        preferences.getInt(
            "daily_day",
            -1
        );

    preferences.end();
}

// ------------------------------------------------------------------
// Save daily state
// ------------------------------------------------------------------

static void saveDailyState(
    const struct tm &timeinfo
)
{
    preferences.begin(
        "GasGuard",
        false
    );

    preferences.putInt(
        "daily_year",
        timeinfo.tm_year
    );

    preferences.putInt(
        "daily_day",
        timeinfo.tm_yday
    );

    preferences.end();

    lastDailySMSYear =
        timeinfo.tm_year;

    lastDailySMSDay =
        timeinfo.tm_yday;
}

// ------------------------------------------------------------------
// Check whether current RTC time is valid
// ------------------------------------------------------------------

static bool getValidLocalTime(
    struct tm &timeinfo
)
{
    if (!getLocalTime(
            &timeinfo,
            100))
    {
        return false;
    }

    // ESP32 time should represent a modern year.
    if (timeinfo.tm_year < 120)
    {
        return false;
    }

    return true;
}

// ------------------------------------------------------------------
// Build gas-level SMS
// ------------------------------------------------------------------

static String buildGasLevelMessage(
    float gasPercent
)
{
    String message;

    message += "GasGuard\n\n";
    message += "Gas Level: ";
    message += String(
        gasPercent,
        1
    );
    message += "%\n";

    return message;
}

// ------------------------------------------------------------------
// Handle user LEVEL request
// ------------------------------------------------------------------

static void handleUserGasRequest()
{
    if (!isUserRequest)
    {
        return;
    }

    // --------------------------------------------------------------
    // Prevent simultaneous calculation.
    // --------------------------------------------------------------

    if (gasMeasurementInProgress)
    {
        return;
    }

    gasMeasurementInProgress = true;

    float gasPercent = NAN;

    bool measurementSuccess =
        measureGasPercentage(
            gasPercent
        );

    if (!measurementSuccess)
    {
        Serial.println(
            "User gas-level measurement failed."
        );

        // Clear request even when HX711 is unavailable.
        isUserRequest = false;

        gasMeasurementInProgress = false;

        return;
    }

    String message =
        buildGasLevelMessage(
            gasPercent
        );

    // --------------------------------------------------------------
    // User-request SMS.
    //
    // sendSingleSMS() itself performs 3 total attempts.
    // --------------------------------------------------------------

    bool smsSuccess =
        sendSingleSMS(message);

    if (!smsSuccess)
    {
        Serial.println(
            "User LEVEL SMS failed after 3 attempts."
        );
    }

    // --------------------------------------------------------------
    // Update low-gas hysteresis state.
    //
    // This does NOT send an automatic alert here.
    // User-requested SMS is independent of automatic alert state.
    // --------------------------------------------------------------

    updateLowGasAlertState(
        gasPercent
    );

    // --------------------------------------------------------------
    // Most important:
    // always clear the user request after handling it.
    // --------------------------------------------------------------

    isUserRequest = false;

    gasMeasurementInProgress = false;
}

// ------------------------------------------------------------------
// Handle automatic low-gas notification
// ------------------------------------------------------------------

static void performDailyGasCheck()
{
    if (gasMeasurementInProgress)
    {
        return;
    }

    gasMeasurementInProgress = true;

    float gasPercent = NAN;

    bool measurementSuccess =
        measureGasPercentage(
            gasPercent
        );

    if (!measurementSuccess)
    {
        Serial.println(
            "Daily gas-level measurement failed."
        );

        gasMeasurementInProgress = false;

        return;
    }

    Serial.print(
        "Daily gas level: "
    );

    Serial.print(
        gasPercent,
        1
    );

    Serial.println("%");

    // --------------------------------------------------------------
    // LOW GAS
    // --------------------------------------------------------------

    if (gasPercent <= GAS_LOW_THRESHOLD_PERCENT)
    {
        if (!isLowGasAlertActive())
        {
            String message;

            message += "GasGuard ALERT!\n\n";
            message += "Gas level is LOW.\n";
            message += "Gas Level: ";
            message += String(
                gasPercent,
                1
            );
            message += "%";

            bool smsSuccess =
                sendMultipleSMS(message);

            if (smsSuccess)
            {
                Serial.println(
                    "Automatic low-gas alert sent."
                );
            }
            else
            {
                Serial.println(
                    "Automatic low-gas alert failed."
                );
            }

            // ------------------------------------------------------
            // Mark the alert state regardless of SMS success.
            //
            // This prevents a GSM failure from causing repeated
            // automatic SMS attempts every time a measurement is
            // triggered while the cylinder remains below 20%.
            // ------------------------------------------------------

            updateLowGasAlertState(
                GAS_LOW_THRESHOLD_PERCENT
            );

            // The helper only clears above recovery threshold,
            // so explicitly activate the state here.
            // ------------------------------------------------------

            // This is intentionally handled below.
        }
        else
        {
            Serial.println(
                "Low-gas alert already sent. "
                "No duplicate SMS."
            );
        }

        // Keep low-alert state active while <=20%.
        // The state is reset only after >30%.
        //
        // Since updateLowGasAlertState() only clears the state,
        // activate it by using the internal notification transition
        // implemented below.
    }
    else
    {
        // ----------------------------------------------------------
        // >20% does not send an SMS.
        // >30% resets the low-gas notification state.
        // ----------------------------------------------------------

        updateLowGasAlertState(
            gasPercent
        );
    }

    gasMeasurementInProgress = false;
}

// ------------------------------------------------------------------
// Daily low-alert state setter
// ------------------------------------------------------------------

static bool automaticLowAlertLatched = false;

static void performDailyGasCheckWithLatch()
{
    if (gasMeasurementInProgress)
    {
        return;
    }

    gasMeasurementInProgress = true;

    float gasPercent = NAN;

    bool measurementSuccess =
        measureGasPercentage(
            gasPercent
        );

    if (!measurementSuccess)
    {
        Serial.println(
            "Daily gas-level measurement failed."
        );

        gasMeasurementInProgress = false;

        return;
    }

    Serial.print(
        "Daily gas level: "
    );

    Serial.print(
        gasPercent,
        1
    );

    Serial.println("%");

    // --------------------------------------------------------------
    // Recovery
    // --------------------------------------------------------------

    if (gasPercent > GAS_LOW_RECOVERY_PERCENT)
    {
        if (automaticLowAlertLatched)
        {
            Serial.println(
                "Gas level recovered above 30%. "
                "Low-gas alert reset."
            );
        }

        automaticLowAlertLatched = false;

        updateLowGasAlertState(
            gasPercent
        );
    }

    // --------------------------------------------------------------
    // Automatic low-gas notification
    // --------------------------------------------------------------

    if (gasPercent <= GAS_LOW_THRESHOLD_PERCENT)
    {
        if (!automaticLowAlertLatched)
        {
            String message;

            message += "GasGuard ALERT!\n\n";
            message += "Gas level is LOW.\n";
            message += "Gas Level: ";
            message += String(
                gasPercent,
                1
            );
            message += "%";

            bool smsSuccess =
                sendMultipleSMS(message);

            if (smsSuccess)
            {
                Serial.println(
                    "Automatic low-gas alert sent."
                );
            }
            else
            {
                Serial.println(
                    "Automatic low-gas alert failed."
                );
            }

            // Latch regardless of SMS success to prevent repeated
            // automatic alerts while the condition remains low.
            automaticLowAlertLatched = true;
        }
        else
        {
            Serial.println(
                "Automatic low-gas alert already sent."
            );
        }
    }

    gasMeasurementInProgress = false;
}

// ------------------------------------------------------------------
// RTC daily scheduler
// ------------------------------------------------------------------

static void checkDailyRTCUpdate()
{
    struct tm timeinfo;

    if (!getValidLocalTime(timeinfo))
    {
        rtcAvailable = false;

        return;
    }

    rtcAvailable = true;

    // --------------------------------------------------------------
    // Only operate during the 10:00–10:04 window.
    // --------------------------------------------------------------

    if (timeinfo.tm_hour != DAILY_CHECK_HOUR)
    {
        return;
    }

    if (timeinfo.tm_min >=
        DAILY_CHECK_WINDOW_MINUTES)
    {
        return;
    }

    // --------------------------------------------------------------
    // Prevent duplicate daily measurements.
    // --------------------------------------------------------------

    if (timeinfo.tm_year ==
            lastDailySMSYear &&
        timeinfo.tm_yday ==
            lastDailySMSDay)
    {
        return;
    }

    Serial.println(
        "10 AM daily gas-level measurement."
    );

    performDailyGasCheckWithLatch();

    saveDailyState(
        timeinfo
    );

    // Restart fallback timer because valid RTC is now available.
    fallbackDailyStartTime = millis();
}

// ------------------------------------------------------------------
// Fallback daily scheduler
// ------------------------------------------------------------------

static void checkDailyFallback()
{
    if (rtcAvailable)
    {
        return;
    }

    if (millis() -
            fallbackDailyStartTime <
        DAILY_FALLBACK_INTERVAL_MS)
    {
        return;
    }

    Serial.println(
        "RTC unavailable. "
        "Performing 24-hour fallback gas measurement."
    );

    performDailyGasCheckWithLatch();

    fallbackDailyStartTime =
        millis();
}

// ------------------------------------------------------------------
// SETUP
// ------------------------------------------------------------------

void setup()
{
    Serial.begin(115200);

    // --------------------------------------------------------------
    // GPIO
    // --------------------------------------------------------------

    pinMode(
        BUZZER,
        OUTPUT
    );

    pinMode(
        LED_NORMAL,
        OUTPUT
    );

    pinMode(
        LED_GASLOW,
        OUTPUT
    );

    pinMode(
        LED_LEAK,
        OUTPUT
    );

    digitalWrite(
        BUZZER,
        LOW
    );

    digitalWrite(
        LED_NORMAL,
        LOW
    );

    digitalWrite(
        LED_GASLOW,
        LOW
    );

    digitalWrite(
        LED_LEAK,
        LOW
    );

    // --------------------------------------------------------------
    // GSM
    // --------------------------------------------------------------

    SIM800L.begin(
        9600,
        SERIAL_8N1,
        GSM_TX,
        GSM_RX
    );

    delay(3000);

    if (modem.restart())
    {
        Serial.println(
            "SIM800L restarted."
        );
    }
    else
    {
        Serial.println(
            "WARNING: SIM800L restart failed."
        );
    }

    if (modem.waitForNetwork())
    {
        Serial.println(
            "Network connected."
        );

        configureSMS();

        rtcAvailable =
            syncRTCFromSIM800();

        if (rtcAvailable)
        {
            Serial.println(
                "Network RTC synchronization successful."
            );
        }
        else
        {
            Serial.println(
                "Network RTC synchronization failed."
            );
        }
    }
    else
    {
        Serial.println(
            "Network unavailable."
        );

        rtcAvailable = false;
    }

    // --------------------------------------------------------------
    // Registered phone numbers
    // --------------------------------------------------------------

    loadRegisteredNumbers();

    // --------------------------------------------------------------
    // Cylinder configuration
    // --------------------------------------------------------------

    loadCylinderType();

    if (!validateCylinderConfiguration(
            cylinderType))
    {
        Serial.println(
            "WARNING: Gas-level measurement disabled "
            "until a valid cylinder type is configured."
        );
    }

    // --------------------------------------------------------------
    // HX711
    // --------------------------------------------------------------

    initializeGasLevel();

    // --------------------------------------------------------------
    // Daily state
    // --------------------------------------------------------------

    loadDailyState();

    // --------------------------------------------------------------
    // Fallback timer
    // --------------------------------------------------------------

    fallbackDailyStartTime =
        millis();

    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "GasGuard startup complete."
    );

    Serial.println(
        "Automatic SMS: only when gas level <= 20%."
    );

    Serial.println(
        "User SMS: LEVEL command."
    );

    Serial.println(
        "Daily measurement: 10:00 AM."
    );

    Serial.println(
        "RTC fallback: every 24 hours."
    );

    Serial.println(
        "========================================"
    );
}

// ------------------------------------------------------------------
// LOOP
// ------------------------------------------------------------------

void loop()
{
    // --------------------------------------------------------------
    // MQ-6
    // --------------------------------------------------------------

    if (millis() -
            previousMQ6Check >=
        MQ6_INTERVAL)
    {
        previousMQ6Check =
            millis();

        checkMQ6();
    }

    // --------------------------------------------------------------
    // Incoming SMS
    // --------------------------------------------------------------

    checkIncomingSMS();

    // --------------------------------------------------------------
    // User LEVEL request
    //
    // This is handled before the daily scheduler so a request
    // cannot execute simultaneously with the daily measurement.
    // --------------------------------------------------------------

    if (isUserRequest)
    {
        handleUserGasRequest();
    }

    // --------------------------------------------------------------
    // RTC-based 10 AM scheduler
    // --------------------------------------------------------------

    checkDailyRTCUpdate();

    // --------------------------------------------------------------
    // 24-hour fallback when RTC is unavailable
    // --------------------------------------------------------------

    checkDailyFallback();

    // --------------------------------------------------------------
    // No long blocking delay here.
    // --------------------------------------------------------------

    delay(10);
}
