#pragma once

#include <Arduino.h>
#include <RTTStream.h>
#include "config.h"

// ── Power state ────────────────────────────────────────────────────────────
extern bool deviceOn;

// ── Modes ──────────────────────────────────────────────────────────────────
enum Mode {
    MODE_TRACKING = 0,
    MODE_TRAINING,
    MODE_THERAPY,
    MODE_COUNT
};

extern const char* modeNames[];

// ── Sub-modes ──────────────────────────────────────────────────────────────
#define TRAINING_SUBMODE_COUNT 2
#define TRACKING_SUBMODE_COUNT 1
#define THERAPY_SUBMODE_COUNT  3

extern const char* trainingSubModes[];
extern const char* trackingSubModes[];
extern const char* therapySubModes[];

// ── State ──────────────────────────────────────────────────────────────────
extern Mode    currentMode;
extern uint8_t trainingSubModeIndex;
extern uint8_t trackingSubModeIndex;
extern uint8_t therapySubModeIndex;

// ── Public API ─────────────────────────────────────────────────────────────
void buttonSetup();
void buttonLoop();
