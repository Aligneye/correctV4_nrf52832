#pragma once

#include <Arduino.h>

void storageSetup();

uint8_t storageLoadTherapySubMode();
void    storageSaveTherapySubMode(uint8_t idx);

void storageLoadCalibration(float* y, float* z);
void storageSaveCalibration(float y, float z);
