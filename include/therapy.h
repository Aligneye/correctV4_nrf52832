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
/** Stops motor + therapy state. If returnToTraining is false, leaves currentMode unchanged (e.g. sub-mode restart). */
void therapyStop(bool returnToTraining = true);

bool therapyIsRunning();
unsigned long therapyGetElapsedMs();
unsigned long therapyGetRemainingMs();
const char* therapyGetCurrentPatternName();
const char* therapyGetNextPatternName();
