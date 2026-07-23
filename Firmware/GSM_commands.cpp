#include <time.h>

extern bool isUserRequest = false;

void configureSMS() {
    SIM800L.println("AT+CMGF=1");      // SMS Text Mode
    delay(500);

    SIM800L.println("AT+CNMI=2,2,0,0,0"); // Send incoming SMS directly to UART
    delay(500);
}

bool syncRTCFromSIM800() {
    String response = "";

    // Enable automatic network time synchronization
    SIM800L.println("AT+CLTS=1"); // Clock Local Time Stamp
    delay(500);

    while (SIM800L.available())
        SIM800L.read();

    // Save the setting
    SIM800L.println("AT&W"); // Write Configuration to Non-Volatile Memory
    delay(500);

    while (SIM800L.available())
        SIM800L.read();

    // Request current date/time
    SIM800L.println("AT+CCLK?"); // Query the Current Clock

    unsigned long start = millis();

    while (millis() - start < 3000)
    {
        while (SIM800L.available())
        {
            char c = SIM800L.read();
            response += c;
        }

        if (response.indexOf("OK") != -1)
            break;
    }

    int firstQuote = response.indexOf('"');
    int secondQuote = response.indexOf('"', firstQuote + 1);

    if (firstQuote == -1 || secondQuote == -1)
    {
        Serial.println("Failed to read network time.");
        return false;
    }

    String timeString = response.substring(firstQuote + 1, secondQuote);

    // Example:
    // 26/07/14,10:23:45+22

    int year, month, day;
    int hour, minute, second;

    sscanf(timeString.c_str(),
           "%d/%d/%d,%d:%d:%d",
           &year,
           &month,
           &day,
           &hour,
           &minute,
           &second);

    struct tm tm;

    tm.tm_year = year + 100;   // 2000 + year - 1900
    tm.tm_mon  = month - 1;
    tm.tm_mday = day;

    tm.tm_hour = hour;
    tm.tm_min  = minute;
    tm.tm_sec  = second;

    time_t t = mktime(&tm);

    struct timeval now = {
        .tv_sec = t,
        .tv_usec = 0
    };

    settimeofday(&now, NULL);

    Serial.println("ESP32 RTC Updated!");

    return true;
}

// --------------------------------------------------------------------------------------
void checkIncomingSMS() {

    if (!SIM800L.available())
        return;

    String response = SIM800L.readString();

    Serial.println(response);   // Debug

    response.toUpperCase();

    if (response.indexOf("LEVEL") != -1)
    {
        isUserRequest = true;
        Serial.println("User requested gas level.");
    }
}

// ---------------------------------------------------------------------------------------
void sendGasPercentageSMS(float gasPercent) {
 
  String message = "";

  message += "GasGuard Alert\n\n";
  message += "Gas Level: ";
  message += String(gasPercent, 1);
  message += "%\n";

  bool success = modem.sendSMS(phoneNumber, message);

  if(success)
    Serial.println("SMS Sent Successfully");
  else
    Serial.println("SMS Failed");
}