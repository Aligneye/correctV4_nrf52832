#pragma once

#include <Arduino.h>
#include <RTTStream.h>
#include "config.h"

enum TherapyState {
    THERAPY_IDLE,
    THERAPY_RUNNING
};

void therapySetup();
void therapyLoop();

void therapyStart();
void therapyStop();

bool therapyIsRunning();
unsigned long therapyGetElapsedMs();
unsigned long therapyGetRemainingMs();
const char* therapyGetCurrentPatternName();
const char* therapyGetNextPatternName();
