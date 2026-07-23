#include <HardwareSerial.h>
#include <TinyGsmClient.h>
#include <time.h>
#include "HX711.h"
#include "GSM_commands.h"

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

// ---------- Constants ----------------
extern bool isUserRequest;
int lastDailySMSDay = -1;

extern char phoneNumber[];
constexpr float calibration_factor = 0.12;

extern int cylinderType;
const float fullWeight[] = {24.5, 12.0, 5.3};
const float emptyWeight[] = {12.0, 7.0, 3.0}:


// ---------------------------------
HardwareSerial SIM800L(2);  // Serial2
TinyGsm modem(SIM800L);
HX711 scale;

// ----------------------------------------------------- Gas weight ---------------------------------------------------
void calcGasPercentage() {

  float currentWeight = scale.get_units(); 

  float gasPercent = ((currentWeight - emptyWeight[cylinderType]) / (fullWeight[cylinderType] - emptyWeight[cylinderType])) * 100.0;
  gasPercent = constrain(gasPercent, 0.0, 100.0);

  if (gasPercent <= 20 || isUserRequest)
    sendGasPercentageSMS(gasPercent);

  if (isUserRequest)
    isUserRequest = false;
}



void checkDailyUpdate()
{
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo))
        return;

    int currentHour = timeinfo.tm_hour;
    int currentMinute = timeinfo.tm_min;
    int currentDay = timeinfo.tm_yday;

    if (currentHour == 10 && currentMinute < 5 && currentDay != lastDailySMSDay) {
        
        calcGasPercentage();
        lastDailySMSDay = currentDay;
    }
}

// --------------------------------------------------------- SETUP -------------------------------------------------------
void setup() {
  Serial.begin(115200);

  SIM800L.begin(9600, SERIAL_8N1, GSM_TX, GSM_RX);
  delay(3000);
  modem.restart();
  if (modem.waitForNetwork()) {
    Serial.println("Network connected");
    syncRTCFromSIM800();
  } else {
    Serial.println("Network failed");
  }

  scale.begin(HX_DT, HX_SCK);
  scale.set_scale(calibration_factor);
  scale.tare();

}

// ------------------------------------------------------ LOOP ------------------------------------------------------
void loop() {
  
  if (isUserRequest)
    calcGasPercentage();

  checkDailyUpdate();
  delay(1000);

}
