#ifndef GAS_LEVEL_H
#define GAS_LEVEL_H

#include <Arduino.h>
#include "HX711.h"

// ------------------------------------------------------------------
// Cylinder configuration
// ------------------------------------------------------------------

extern int cylinderType;

bool validateCylinderConfiguration(int type);

void loadCylinderType();

bool saveCylinderType(int type);

void getCylinderType(String response);

bool processCylinderTypeConfirmation(String response);

// ------------------------------------------------------------------
// HX711 / gas-level measurement
// ------------------------------------------------------------------

void initializeGasLevel();

bool performFirstStartupTare();

bool readGasWeight(float &weightKg);

bool calculateGasPercentage(float weightKg, float &gasPercent);

// ------------------------------------------------------------------
// Gas-level notification state
// ------------------------------------------------------------------

bool isLowGasAlertActive();

void updateLowGasAlertState(float gasPercent);

// ------------------------------------------------------------------
// Complete measurement
// ------------------------------------------------------------------

bool measureGasPercentage(float &gasPercent);

#endif