#pragma once

#include <Arduino.h>

void motorSetup();
void motorSetDuty(uint8_t duty);  // 0 = off, 255 = full on
void motorUpdate();                // call frequently from loop()
