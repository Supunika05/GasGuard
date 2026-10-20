#include <Arduino.h>
#include <Preferences.h>
#include <HardwareSerial.h>
#include <sys/time.h>
#include <time.h>

#define TINY_GSM_MODEM_SIM800
#include <TinyGsmClient.h>

#include "Config.h"
#include "GSM_commands.h"
#include "GasLevel.h"

// ------------------------------------------------------------------
// External GSM objects
// ------------------------------------------------------------------

extern HardwareSerial SIM800L;
extern TinyGsm modem;

// ------------------------------------------------------------------
// Global GSM state
// ------------------------------------------------------------------

String currentSender = "";

bool isUserRequest = false;

String registeredNumbers[MAX_USERS];

// ------------------------------------------------------------------
// Configure SMS
// ------------------------------------------------------------------

void configureSMS()
{
    SIM800L.println("AT+CMGF=1");
    delay(500);

    SIM800L.println("AT+CNMI=2,2,0,0,0");
    delay(500);
}

// ------------------------------------------------------------------
// Synchronize ESP32 RTC from SIM800L
// ------------------------------------------------------------------

bool syncRTCFromSIM800()
{
    String response = "";

    SIM800L.println("AT+CLTS=1");
    delay(500);

    while (SIM800L.available())
    {
        SIM800L.read();
    }

    SIM800L.println("AT&W");
    delay(500);

    while (SIM800L.available())
    {
        SIM800L.read();
    }

    SIM800L.println("AT+CCLK?");

    unsigned long start = millis();

    while (millis() - start < 3000)
    {
        while (SIM800L.available())
        {
            char c = SIM800L.read();
            response += c;
        }

        if (response.indexOf("OK") != -1)
        {
            break;
        }
    }

    int firstQuote = response.indexOf('"');

    int secondQuote =
        response.indexOf('"', firstQuote + 1);

    if (firstQuote == -1 ||
        secondQuote == -1)
    {
        Serial.println(
            "Failed to read network time."
        );

        return false;
    }

    String timeString =
        response.substring(
            firstQuote + 1,
            secondQuote
        );

    int year;
    int month;
    int day;

    int hour;
    int minute;
    int second;

    int parsed =
        sscanf(
            timeString.c_str(),
            "%d/%d/%d,%d:%d:%d",
            &year,
            &month,
            &day,
            &hour,
            &minute,
            &second
        );

    if (parsed != 6)
    {
        Serial.println(
            "ERROR: Could not parse SIM800L time."
        );

        return false;
    }

    struct tm tmInfo = {};

    tmInfo.tm_year = year + 100;
    tmInfo.tm_mon = month - 1;
    tmInfo.tm_mday = day;

    tmInfo.tm_hour = hour;
    tmInfo.tm_min = minute;
    tmInfo.tm_sec = second;

    time_t t = mktime(&tmInfo);

    if (t == (time_t)-1)
    {
        Serial.println(
            "ERROR: Failed to convert network time."
        );

        return false;
    }

    struct timeval now =
    {
        .tv_sec = t,
        .tv_usec = 0
    };

    settimeofday(&now, nullptr);

    Serial.println(
        "ESP32 RTC updated from SIM800L."
    );

    return true;
}

// ------------------------------------------------------------------
// Get sender number
// ------------------------------------------------------------------

String getSenderNumber(String response)
{
    int firstQuote =
        response.indexOf('"');

    if (firstQuote == -1)
    {
        return "";
    }

    int secondQuote =
        response.indexOf(
            '"',
            firstQuote + 1
        );

    if (secondQuote == -1)
    {
        return "";
    }

    return response.substring(
        firstQuote + 1,
        secondQuote
    );
}

// ------------------------------------------------------------------
// Send SMS with 3 total attempts
// ------------------------------------------------------------------

static bool sendSMSWithRetry(
    const String &phoneNumber,
    const String &message
)
{
    if (phoneNumber == "")
    {
        return false;
    }

    for (uint8_t attempt = 1;
         attempt <= SMS_MAX_ATTEMPTS;
         attempt++)
    {
        Serial.print("SMS attempt ");
        Serial.print(attempt);
        Serial.print("/");
        Serial.println(SMS_MAX_ATTEMPTS);

        bool success =
            modem.sendSMS(
                phoneNumber,
                message
            );

        if (success)
        {
            Serial.println(
                "SMS sent successfully."
            );

            return true;
        }

        Serial.println(
            "SMS attempt failed."
        );

        if (attempt < SMS_MAX_ATTEMPTS)
        {
            delay(SMS_RETRY_DELAY_MS);
        }
    }

    Serial.println(
        "SMS failed after 3 total attempts."
    );

    return false;
}

// ------------------------------------------------------------------
// Send SMS to current sender
// ------------------------------------------------------------------

bool sendSingleSMS(String message)
{
    if (currentSender == "")
    {
        Serial.println(
            "ERROR: No current SMS sender."
        );

        return false;
    }

    return sendSMSWithRetry(
        currentSender,
        message
    );
}

// ------------------------------------------------------------------
// Send SMS to all registered users
// ------------------------------------------------------------------

bool sendMultipleSMS(String message)
{
    bool atLeastOneSuccess = false;

    for (uint8_t i = 0;
         i < MAX_USERS;
         i++)
    {
        if (registeredNumbers[i] == "")
        {
            continue;
        }

        bool success =
            sendSMSWithRetry(
                registeredNumbers[i],
                message
            );

        if (success)
        {
            atLeastOneSuccess = true;

            Serial.print(
                "SMS sent to "
            );

            Serial.println(
                registeredNumbers[i]
            );
        }
        else
        {
            Serial.print(
                "Failed to send SMS to "
            );

            Serial.println(
                registeredNumbers[i]
            );
        }

        delay(1000);
    }

    return atLeastOneSuccess;
}

// ------------------------------------------------------------------
// Load registered numbers
// ------------------------------------------------------------------

void loadRegisteredNumbers()
{
    Preferences preferences;

    preferences.begin(
        "GasGuard",
        true
    );

    for (uint8_t i = 0;
         i < MAX_USERS;
         i++)
    {
        String key =
            "phone" + String(i);

        registeredNumbers[i] =
            preferences.getString(
                key.c_str(),
                ""
            );
    }

    preferences.end();

    Serial.println(
        "Registered Numbers:"
    );

    for (uint8_t i = 0;
         i < MAX_USERS;
         i++)
    {
        if (registeredNumbers[i] != "")
        {
            Serial.print("User ");
            Serial.print(i + 1);
            Serial.print(": ");
            Serial.println(
                registeredNumbers[i]
            );
        }
    }
}

// ------------------------------------------------------------------
// Check registered sender
// ------------------------------------------------------------------

bool isRegistered()
{
    if (currentSender == "")
    {
        return false;
    }

    for (uint8_t i = 0;
         i < MAX_USERS;
         i++)
    {
        if (registeredNumbers[i] ==
            currentSender)
        {
            return true;
        }
    }

    return false;
}

// ------------------------------------------------------------------
// Save phone number
// ------------------------------------------------------------------

bool savePhoneNumber(
    String phoneNumber,
    int index
)
{
    if (index < 0 ||
        index >= MAX_USERS)
    {
        return false;
    }

    Preferences preferences;

    preferences.begin(
        "GasGuard",
        false
    );

    String key =
        "phone" + String(index);

    bool success =
        preferences.putString(
            key.c_str(),
            phoneNumber
        ) > 0;

    preferences.end();

    return success;
}

// ------------------------------------------------------------------
// Register phone number
// ------------------------------------------------------------------

bool registerPhoneNumber()
{
    if (currentSender == "")
    {
        return false;
    }

    if (isRegistered())
    {
        Serial.println(
            "Already registered."
        );

        sendSingleSMS(
            "Already registered."
        );

        return false;
    }

    for (uint8_t i = 0;
         i < MAX_USERS;
         i++)
    {
        if (registeredNumbers[i] == "")
        {
            if (savePhoneNumber(
                    currentSender,
                    i))
            {
                registeredNumbers[i] =
                    currentSender;

                Serial.print(
                    "Phone number registered: "
                );

                Serial.println(
                    currentSender
                );

                sendSingleSMS(
                    "Phone number registered."
                );

                return true;
            }

            Serial.println(
                "Failed to save phone number."
            );

            sendSingleSMS(
                "Failed to save phone number."
            );

            return false;
        }
    }

    Serial.println(
        "Maximum number of users reached."
    );

    sendSingleSMS(
        "Maximum number of users reached."
    );

    return false;
}

// ------------------------------------------------------------------
// Check incoming SMS
// ------------------------------------------------------------------

void checkIncomingSMS()
{
    if (!SIM800L.available())
    {
        return;
    }

    String response =
        SIM800L.readString();

    String senderNumber =
        getSenderNumber(response);

    if (senderNumber == "")
    {
        Serial.println(
            "WARNING: Could not identify SMS sender."
        );

        return;
    }

    currentSender = senderNumber;

    Serial.print("Sender: ");
    Serial.println(currentSender);

    response.toUpperCase();

    // --------------------------------------------------------------
    // Registration
    // --------------------------------------------------------------

    if (response.indexOf(
            "REGISTER 1234") != -1)
    {
        registerPhoneNumber();

        return;
    }

    if (response.indexOf(
            "REGISTER") != -1)
    {
        sendSingleSMS(
            "Enter PIN number and resend SMS."
        );

        return;
    }

    // --------------------------------------------------------------
    // All configuration and LEVEL commands below require
    // a registered sender.
    // --------------------------------------------------------------

    if (!isRegistered())
    {
        Serial.println(
            "Ignoring command from unregistered number."
        );

        return;
    }

    // --------------------------------------------------------------
    // Cylinder type confirmation must be checked first.
    // --------------------------------------------------------------

    if (processCylinderTypeConfirmation(
            response))
    {
        return;
    }

    // --------------------------------------------------------------
    // TYPE command
    // --------------------------------------------------------------

    if (response.indexOf("TYPE") != -1)
    {
        getCylinderType(response);

        return;
    }

    // --------------------------------------------------------------
    // LEVEL request
    // --------------------------------------------------------------

    if (response.indexOf("LEVEL") != -1)
    {
        isUserRequest = true;

        Serial.println(
            "User gas-level request received."
        );

        return;
    }
}