#ifndef GSM_COMMANDS_H
#define GSM_COMMANDS_H

#include <Arduino.h>

constexpr uint8_t MAX_USERS = 4;

extern String currentSender;
extern bool isUserRequest;

extern String registeredNumbers[MAX_USERS];

void configureSMS();

bool syncRTCFromSIM800();

String getSenderNumber(String response);

bool sendSingleSMS(String message);

bool sendMultipleSMS(String message);

void loadRegisteredNumbers();

void checkIncomingSMS();

bool isRegistered();

bool registerPhoneNumber();

bool savePhoneNumber(String phoneNumber, int index);

#endif