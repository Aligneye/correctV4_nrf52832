#pragma once

#include <Arduino.h>
#include <RTTStream.h>
#include "config.h"

// ── Power state ────────────────────────────────────────────────────────────
extern bool deviceOn;

// ── Modes ──────────────────────────────────────────────────────────────────
enum Mode {
    MODE_TRAINING = 0,
    MODE_THERAPY,
    MODE_OFF,
    MODE_COUNT
};

extern const char* modeNames[];

// ── Sub-modes ──────────────────────────────────────────────────────────────
#define TRAINING_SUBMODE_COUNT 3
#define THERAPY_SUBMODE_COUNT  3

extern const char* trainingSubModes[];
extern const char* therapySubModes[];

// ── State ──────────────────────────────────────────────────────────────────
extern Mode    currentMode;
extern uint8_t trainingSubModeIndex;
extern uint8_t therapySubModeIndex;

// ── Public API ─────────────────────────────────────────────────────────────
void buttonSetup();
void buttonLoop();
