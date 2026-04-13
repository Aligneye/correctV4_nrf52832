#pragma once

// ============================================================
//  AlignEye Firmware V4 — Board Pin Configuration
//  Custom nRF52832 PCB
// ============================================================

// ── Button ────────────────────────────────────────────────
#define PIN_BUTTON      0   // P0.11 → Arduino 0

// ── RGB LED ───────────────────────────────────────────────
#define PIN_LED_RED     2   // P0.13 → Arduino 2
#define PIN_LED_GREEN   3   // P0.14 → Arduino 3
#define PIN_LED_BLUE    4  // P0.15 → Arduino 4

// ── Motor ─────────────────────────────────────────────────
#define PIN_MOTOR       6   // P0.17 → Arduino 6

// ── I2C — LIS3DH Accelerometer ────────────────────────────
// Handled by Wire library automatically on nRF52832
// SDA → P0.26, SCL → P0.27

// ── Reset ─────────────────────────────────────────────────
// P0.21 → hardware reset, not used in firmware

// ── Battery ADC ───────────────────────────────────────────
// No fixed pin assigned yet

// ── Button timing (ms) ────────────────────────────────────
#define DEBOUNCE_MS         50
#define DOUBLE_CLICK_GAP_MS 400
#define HOLD_MS             1000

// ── Vibration intensity (PWM 0-255) ───────────────────────
#define VIB_INTENSITY_LOW   160
#define VIB_INTENSITY_MID   200
#define VIB_INTENSITY_HIGH  230
#define VIB_INTENSITY_MAX   255

// ── Therapy durations (ms) ────────────────────────────────
#define THERAPY_DURATION_5_MIN   300000UL
#define THERAPY_DURATION_10_MIN  600000UL
#define THERAPY_DURATION_20_MIN  1200000UL
#define THERAPY_PATTERN_MS       120000UL  // each pattern runs 2 min

// ── Therapy patterns (10 total) ───────────────────────────
enum TherapyPattern {
    PATTERN_MUSCLE_ACTIVATION = 0,  // always first
    PATTERN_REVERSE_RAMP,
    PATTERN_RAMP_PATTERN,
    PATTERN_WAVE_THERAPY,
    PATTERN_SLOW_WAVE,
    PATTERN_SINUSOIDAL_WAVE,
    PATTERN_TRIANGLE_WAVE,
    PATTERN_DOUBLE_WAVE,
    PATTERN_ANTI_FATIGUE,
    PATTERN_PULSE_RAMP,
    PATTERN_COUNT
};
