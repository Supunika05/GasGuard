#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

#include "HX711.h"
#include "Config.h"
#include "GasLevel.h"
#include "GSM_commands.h"

// ------------------------------------------------------------------
// External objects
// ------------------------------------------------------------------

extern HX711 scale;
extern Preferences preferences;

// ------------------------------------------------------------------
// Cylinder configuration
// ------------------------------------------------------------------

int cylinderType = -1;

static const float fullWeight[CYLINDER_TYPE_COUNT] =
{
    24.5f,
    12.0f,
    5.3f,
    0.2f
};

static const float emptyWeight[CYLINDER_TYPE_COUNT] =
{
    12.0f,
    7.0f,
    3.0f,
    0.05f
};

// Calibration factor supplied by the existing firmware.
static constexpr float CALIBRATION_FACTOR = 0.12f;

// ------------------------------------------------------------------
// Low-gas notification state
// ------------------------------------------------------------------

static bool lowGasAlertActive = false;

// ------------------------------------------------------------------
// Internal helpers
// ------------------------------------------------------------------

static bool isValidCylinderType(int type)
{
    return type >= 0 && type < CYLINDER_TYPE_COUNT;
}

static bool isValidFinite(float value)
{
    return isfinite(value);
}

// ------------------------------------------------------------------
// Cylinder configuration validation
// ------------------------------------------------------------------

bool validateCylinderConfiguration(int type)
{
    if (!isValidCylinderType(type))
    {
        Serial.println("ERROR: Invalid cylinder type.");
        return false;
    }

    if (!isValidFinite(fullWeight[type]) ||
        !isValidFinite(emptyWeight[type]))
    {
        Serial.println("ERROR: Invalid cylinder weight configuration.");
        return false;
    }

    if (fullWeight[type] <= emptyWeight[type])
    {
        Serial.println(
            "ERROR: Full cylinder weight must be greater than empty weight."
        );

        return false;
    }

    return true;
}

// ------------------------------------------------------------------
// Save cylinder type
// ------------------------------------------------------------------

bool saveCylinderType(int type)
{
    if (!validateCylinderConfiguration(type))
    {
        return false;
    }

    preferences.begin("GasGuard", false);

    bool success =
        preferences.putInt("cylinder_type", type) == sizeof(int);

    preferences.end();

    if (success)
    {
        cylinderType = type;
    }

    return success;
}

// ------------------------------------------------------------------
// Load cylinder type
// ------------------------------------------------------------------

void loadCylinderType()
{
    preferences.begin("GasGuard", true);

    int storedType =
        preferences.getInt("cylinder_type", -1);

    preferences.end();

    if (!validateCylinderConfiguration(storedType))
    {
        cylinderType = -1;

        Serial.println(
            "WARNING: No valid cylinder configuration."
        );

        return;
    }

    cylinderType = storedType;

    Serial.print("Loaded cylinder type: ");
    Serial.println(cylinderType + 1);
}

// ------------------------------------------------------------------
// Cylinder type confirmation
// ------------------------------------------------------------------

static int pendingCylinderType = -1;
static String pendingCylinderSender = "";

static void clearPendingCylinderType()
{
    pendingCylinderType = -1;
    pendingCylinderSender = "";
}

// ------------------------------------------------------------------
// Process TYPE command
// ------------------------------------------------------------------

void getCylinderType(String response)
{
    int typeIndex = response.indexOf("TYPE");

    if (typeIndex < 0)
    {
        return;
    }

    int numberStart = typeIndex + 4;

    while (numberStart < response.length() &&
           response[numberStart] == ' ')
    {
        numberStart++;
    }

    int numberEnd = numberStart;

    while (numberEnd < response.length() &&
           isDigit(response[numberEnd]))
    {
        numberEnd++;
    }

    if (numberEnd == numberStart)
    {
        Serial.println("No cylinder type number found.");

        sendSingleSMS(
            "No cylinder type number found."
        );

        return;
    }

    String typeString =
        response.substring(numberStart, numberEnd);

    int requestedType = typeString.toInt() - 1;

    if (!validateCylinderConfiguration(requestedType))
    {
        Serial.println("Invalid cylinder type.");

        sendSingleSMS(
            "Invalid cylinder type."
        );

        return;
    }

    // Store pending request.
    pendingCylinderType = requestedType;
    pendingCylinderSender = currentSender;

    String message;

    message += "GasGuard\n\n";
    message += "Proposed cylinder type: ";
    message += String(requestedType + 1);
    message += "\n\n";
    message += "Reply CONFIRM to apply this change.";

    sendSingleSMS(message);

    Serial.print("Pending cylinder type: ");
    Serial.println(requestedType + 1);
}

// ------------------------------------------------------------------
// Confirm pending cylinder type
// ------------------------------------------------------------------

bool processCylinderTypeConfirmation(String response)
{
    if (pendingCylinderType < 0)
    {
        return false;
    }

    if (currentSender == "" ||
        currentSender != pendingCylinderSender)
    {
        return false;
    }

    if (response.indexOf("CONFIRM") == -1)
    {
        return false;
    }

    if (!validateCylinderConfiguration(pendingCylinderType))
    {
        clearPendingCylinderType();

        sendSingleSMS(
            "Cylinder configuration is invalid."
        );

        return true;
    }

    int newType = pendingCylinderType;

    if (saveCylinderType(newType))
    {
        String message;

        message += "GasGuard\n\n";
        message += "Cylinder type ";
        message += String(newType + 1);
        message += " saved successfully.";

        sendSingleSMS(message);

        Serial.print("Cylinder type changed to: ");
        Serial.println(newType + 1);
    }
    else
    {
        sendSingleSMS(
            "Failed to save cylinder type."
        );
    }

    clearPendingCylinderType();

    return true;
}

// ------------------------------------------------------------------
// First-startup tare
// ------------------------------------------------------------------

bool performFirstStartupTare()
{
    preferences.begin("GasGuard", false);

    bool tareAlreadyDone =
        preferences.getBool("tare_done", false);

    preferences.end();

    if (tareAlreadyDone)
    {
        Serial.println(
            "HX711 tare already performed previously."
        );

        Serial.println(
            "Skipping tare because the cylinder/platform may be installed."
        );

        return true;
    }

    Serial.println();
    Serial.println("========================================");
    Serial.println("FIRST STARTUP TARE");
    Serial.println("========================================");
    Serial.println(
        "Make sure the load-cell platform is installed."
    );
    Serial.println(
        "The cylinder must NOT be on the platform."
    );

    unsigned long start = millis();

    while (!scale.is_ready())
    {
        if (millis() - start >= TARE_TIMEOUT_MS)
        {
            Serial.println(
                "ERROR: HX711 unavailable. Tare failed."
            );

            return false;
        }

        delay(20);
    }

    scale.tare();

    preferences.begin("GasGuard", false);

    bool success =
        preferences.putBool("tare_done", true);

    preferences.end();

    if (!success)
    {
        Serial.println(
            "ERROR: Failed to save tare state."
        );

        return false;
    }

    Serial.println("First-startup tare completed.");

    return true;
}

// ------------------------------------------------------------------
// Initialize gas-level system
// ------------------------------------------------------------------

void initializeGasLevel()
{
    scale.begin(HX_DT, HX_SCK);

    scale.set_scale(CALIBRATION_FACTOR);

    if (!performFirstStartupTare())
    {
        Serial.println(
            "WARNING: First-startup tare was not completed."
        );
    }
}

// ------------------------------------------------------------------
// Read multiple HX711 measurements
// ------------------------------------------------------------------

bool readGasWeight(float &weightKg)
{
    weightKg = NAN;

    unsigned long startTime = millis();

    float readings[GAS_WEIGHT_SAMPLES];

    uint8_t validCount = 0;

    while (validCount < GAS_WEIGHT_SAMPLES)
    {
        if (millis() - startTime >=
            GAS_MEASUREMENT_TIMEOUT_MS)
        {
            Serial.println(
                "ERROR: Gas measurement timeout."
            );

            return false;
        }

        if (!scale.is_ready())
        {
            delay(10);
            continue;
        }

        float reading = scale.get_units(1);

        if (!isValidFinite(reading))
        {
            Serial.println(
                "WARNING: Invalid HX711 reading."
            );

            continue;
        }

        // Reject physically impossible individual readings.
        if (reading < MIN_VALID_WEIGHT_KG ||
            reading > MAX_VALID_WEIGHT_KG)
        {
            Serial.print(
                "WARNING: Rejected HX711 reading: "
            );

            Serial.println(reading);

            continue;
        }

        readings[validCount] = reading;
        validCount++;
    }

    // --------------------------------------------------------------
    // Sort readings for simple outlier rejection.
    // --------------------------------------------------------------

    for (uint8_t i = 0; i < validCount - 1; i++)
    {
        for (uint8_t j = i + 1; j < validCount; j++)
        {
            if (readings[j] < readings[i])
            {
                float temp = readings[i];

                readings[i] = readings[j];
                readings[j] = temp;
            }
        }
    }

    // --------------------------------------------------------------
    // Reject the lowest and highest readings.
    // With 10 samples this leaves 8 readings.
    // --------------------------------------------------------------

    uint8_t firstIndex = 0;
    uint8_t lastIndex = validCount;

    if (validCount >= 5)
    {
        firstIndex = 1;
        lastIndex = validCount - 1;
    }

    float sum = 0.0f;
    uint8_t count = 0;

    for (uint8_t i = firstIndex; i < lastIndex; i++)
    {
        sum += readings[i];
        count++;
    }

    if (count == 0)
    {
        Serial.println(
            "ERROR: No valid HX711 readings."
        );

        return false;
    }

    float average = sum / static_cast<float>(count);

    if (!isValidFinite(average))
    {
        Serial.println(
            "ERROR: Invalid average weight."
        );

        return false;
    }

    // --------------------------------------------------------------
    // Final physical validation.
    // --------------------------------------------------------------

    if (average < MIN_VALID_WEIGHT_KG ||
        average > MAX_VALID_WEIGHT_KG)
    {
        Serial.print(
            "ERROR: Average weight outside valid range: "
        );

        Serial.println(average);

        return false;
    }

    // Small negative values are measurement noise.
    if (average < 0.0f)
    {
        average = 0.0f;
    }

    weightKg = average;

    Serial.print("Measured gas weight: ");
    Serial.print(weightKg, 2);
    Serial.println(" kg");

    return true;
}

// ------------------------------------------------------------------
// Calculate gas percentage
// ------------------------------------------------------------------

bool calculateGasPercentage(float weightKg, float &gasPercent)
{
    gasPercent = NAN;

    // --------------------------------------------------------------
    // Validate cylinder type BEFORE accessing arrays.
    // --------------------------------------------------------------

    if (!validateCylinderConfiguration(cylinderType))
    {
        Serial.println(
            "ERROR: Cannot calculate gas percentage: "
            "invalid cylinder configuration."
        );

        return false;
    }

    // --------------------------------------------------------------
    // Validate measured weight.
    // --------------------------------------------------------------

    if (!isValidFinite(weightKg))
    {
        Serial.println(
            "ERROR: Weight is not finite."
        );

        return false;
    }

    if (weightKg < MIN_VALID_WEIGHT_KG ||
        weightKg > MAX_VALID_WEIGHT_KG)
    {
        Serial.println(
            "ERROR: Weight outside physically valid range."
        );

        return false;
    }

    if (weightKg < 0.0f)
    {
        weightKg = 0.0f;
    }

    const float full =
        fullWeight[cylinderType];

    const float empty =
        emptyWeight[cylinderType];

    const float denominator =
        full - empty;

    // --------------------------------------------------------------
    // Protect percentage calculation.
    // --------------------------------------------------------------

    if (!isValidFinite(denominator) ||
        denominator <= 0.0f)
    {
        Serial.println(
            "ERROR: Invalid gas-percentage denominator."
        );

        return false;
    }

    float percentage =
        ((weightKg - empty) / denominator) * 100.0f;

    if (!isValidFinite(percentage))
    {
        Serial.println(
            "ERROR: Calculated gas percentage is invalid."
        );

        return false;
    }

    // --------------------------------------------------------------
    // Clamp only AFTER all measurement validation.
    // --------------------------------------------------------------

    percentage =
        constrain(percentage, 0.0f, 100.0f);

    gasPercent = percentage;

    Serial.print("Gas level: ");
    Serial.print(gasPercent, 1);
    Serial.println("%");

    return true;
}

// ------------------------------------------------------------------
// Complete measurement
// ------------------------------------------------------------------

bool measureGasPercentage(float &gasPercent)
{
    gasPercent = NAN;

    if (!validateCylinderConfiguration(cylinderType))
    {
        return false;
    }

    float weightKg = NAN;

    if (!readGasWeight(weightKg))
    {
        return false;
    }

    if (!calculateGasPercentage(weightKg, gasPercent))
    {
        return false;
    }

    return true;
}

// ------------------------------------------------------------------
// Low-gas state
// ------------------------------------------------------------------

bool isLowGasAlertActive()
{
    return lowGasAlertActive;
}

// ------------------------------------------------------------------
// Update hysteresis state
// ------------------------------------------------------------------

void updateLowGasAlertState(float gasPercent)
{
    if (!isValidFinite(gasPercent))
    {
        return;
    }

    if (gasPercent > GAS_LOW_RECOVERY_PERCENT)
    {
        if (lowGasAlertActive)
        {
            Serial.println(
                "Gas level recovered. "
                "Low-gas alert state reset."
            );
        }

        lowGasAlertActive = false;
    }
}