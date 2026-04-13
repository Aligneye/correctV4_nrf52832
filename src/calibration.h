#pragma once

#include <Arduino.h>
#include <RTTStream.h>
#include "config.h"

void calibrationSetup();
void calibrationLoop();

void calibrationStart();
void calibrationStop();
bool calibrationIsActive();
