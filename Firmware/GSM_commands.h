#pragma once // Prevent duplicate declarations
#include <time.h>

extern bool isUserRequest;

void configureSMS();
bool syncRTCFromSIM800();
void sendGasPercentageSMS(float gasPercent);
void checkIncomingSMS();