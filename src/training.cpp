#include "training.h"
#include "button.h"
#include "storage.h"
#include "motor.h"
#include "calibration.h"
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <math.h>
#include <string.h>

extern RTTStream rtt;

// ── Spec constants ─────────────────────────────────────────────────────────
static constexpr float kLpfAlpha          = 0.1f;
static constexpr float kMotionThreshold   = 2.0f;
static constexpr float kDirectionDeg      = 20.0f;
static constexpr float kBadPostureDeg     = 25.0f;
static constexpr float kAngleClampDeg     = 90.0f;
static constexpr float kDefaultOriginY    = 6.75f;
static constexpr float kDefaultOriginZ    = 6.75f;
static constexpr float kNearZero          = 0.1f;
static constexpr uint32_t kGoodDebounceMs = 100UL;
static constexpr int     kInitMaxAttempts = 5;
static constexpr uint32_t kInitRetryDelayMs = 200UL;

static Adafruit_LIS3DH lis = Adafruit_LIS3DH();

float rawX = 0, rawY = 0, rawZ = 0;
float Y_ORIGIN = kDefaultOriginY;
float Z_ORIGIN = kDefaultOriginZ;

float currentAngle = 0.0f;
bool   isBadPosture = false;
bool   sensorInitialized = false;

char orientationText[16] = "UNKNOWN";
char directionText[16] = "UNKNOWN";
char postureText[96]     = "UNKNOWN";

static bool _moving = false;

/** Filtered accel (α=0.1) — same seed rule as ESP32 posture_training.cpp */
static float g_fx = 0, g_fy = 0, g_fz = 0;

/** Motion deltas in updatePostureAngle — reset when origin changes so recalibration does not glitch. */
static float s_motionPrevX = 0, s_motionPrevY = 0, s_motionPrevZ = 0;

/** Good-posture debounce timer — reset with new origin. */
static unsigned long s_goodPostureStableStart = 0;

static void trainingIngestAccelSample(void) {
    if (!sensorInitialized) return;

    sensors_event_t e;
    lis.getEvent(&e);

    rawX = e.acceleration.x;
    rawY = e.acceleration.y;
    rawZ = e.acceleration.z;

    if (g_fx == 0.0f && g_fy == 0.0f && g_fz == 0.0f) {
        g_fx = rawX;
        g_fy = rawY;
        g_fz = rawZ;
    } else {
        g_fx = kLpfAlpha * rawX + (1.0f - kLpfAlpha) * g_fx;
        g_fy = kLpfAlpha * rawY + (1.0f - kLpfAlpha) * g_fy;
        g_fz = kLpfAlpha * rawZ + (1.0f - kLpfAlpha) * g_fz;
    }
}

bool trainingSampleAccelForCalibration(void) {
    trainingIngestAccelSample();
    return sensorInitialized;
}

void trainingGetFilteredAccel(float* outY, float* outZ) {
    if (outY) *outY = g_fy;
    if (outZ) *outZ = g_fz;
}

// Session stats (training mode)
static uint32_t s_sessionStartMs     = 0;
static uint32_t s_sessionNumber      = 0;
static uint32_t s_badPostureCount    = 0;
static bool     s_wasBadPosture      = false;
static Mode     s_lastModeForSession = MODE_OFF;

static unsigned long s_lastSensorRttMs = 0;
static unsigned long s_badMotorStartMs = 0;
static unsigned long s_vibToggleMs     = 0;
static bool           s_vibOn          = false;

/** Motor uses same rule as ESP32 isBadPosture (forward > 25° + debounce). */
static bool s_forwardMotorBad = false;

static void loadStoredCalibration() {
    float loadedY = kDefaultOriginY;
    float loadedZ = kDefaultOriginZ;
    storageLoadCalibration(&loadedY, &loadedZ);
    if (fabsf(loadedY) < kNearZero && fabsf(loadedZ) < kNearZero) {
        loadedY = kDefaultOriginY;
        loadedZ = kDefaultOriginZ;
    } else {
        if (fabsf(loadedY) < kNearZero) {
            loadedY = kDefaultOriginY;
        }
        if (fabsf(loadedZ) < kNearZero) {
            loadedZ = kDefaultOriginZ;
        }
    }
    Y_ORIGIN = loadedY;
    Z_ORIGIN = loadedZ;
}

void setPostureOrigin(float y, float z) {
    if (fabsf(y) < kNearZero && fabsf(z) < kNearZero) {
        y = kDefaultOriginY;
        z = kDefaultOriginZ;
    }
    Y_ORIGIN = fabsf(y);
    Z_ORIGIN = z;
    storageSaveCalibration(Y_ORIGIN, Z_ORIGIN);

    /*
     * Recalibration only updates origin; without re-seeding, g_f* still reflect the old
     * low-pass state so angle vs the new origin is wrong until the filter converges.
     * Seed LPF from the calibration vector (y,z) and last raw X; align motion baseline.
     */
    g_fx               = rawX;
    g_fy               = y;
    g_fz               = z;
    s_motionPrevX      = rawX;
    s_motionPrevY      = rawY;
    s_motionPrevZ      = rawZ;
    s_goodPostureStableStart = 0;
    isBadPosture             = false;
    s_wasBadPosture          = false;
}

/** ESP32 computePostureAngle: vertical frame, atan2 delta vs origin, clamp ±90° (orientation only here). */
static float computePostureAngle(float Y, float Z) {
    const bool isVertical = (Y > 0.0f);
    strncpy(orientationText, isVertical ? "VERTICAL" : "INVERTED",
            sizeof(orientationText) - 1);
    orientationText[sizeof(orientationText) - 1] = '\0';

    const float effY = isVertical ? Y : -Y;

    const float currentAngleAbs = atan2f(Z, effY) * (180.0f / (float)M_PI);
    const float originAngleAbs  = atan2f(Z_ORIGIN, Y_ORIGIN) * (180.0f / (float)M_PI);
    float angle                 = currentAngleAbs - originAngleAbs;

    if (angle > kAngleClampDeg) {
        angle = kAngleClampDeg;
    }
    if (angle < -kAngleClampDeg) {
        angle = -kAngleClampDeg;
    }

    return angle;
}

void initPostureSensor() {
    Wire.begin();
    for (int attempt = 0; attempt < kInitMaxAttempts; attempt++) {
        if (lis.begin(0x18) || lis.begin(0x19)) {
            lis.setRange(LIS3DH_RANGE_2_G);
            lis.setDataRate(LIS3DH_DATARATE_100_HZ);
            sensorInitialized = true;
            loadStoredCalibration();
            rtt.println("LIS3DH: OK (±2G, 100Hz)");
            return;
        }
        delay(kInitRetryDelayMs);
    }
    sensorInitialized = false;
    rtt.println("LIS3DH: init failed (will retry on wake)");
}

void updatePostureAngle() {
    if (!sensorInitialized) return;

    trainingIngestAccelSample();

    const float Y = g_fy;
    const float Z = g_fz;

    const float dx = fabsf(rawX - s_motionPrevX);
    const float dy = fabsf(rawY - s_motionPrevY);
    const float dz = fabsf(rawZ - s_motionPrevZ);
    s_motionPrevX  = rawX;
    s_motionPrevY  = rawY;
    s_motionPrevZ  = rawZ;
    const float motionStrength = dx + dy + dz;
    _moving                    = (motionStrength > kMotionThreshold);

    currentAngle = computePostureAngle(Y, Z);

    if (currentAngle > kDirectionDeg) {
        strncpy(directionText, "FORWARD", sizeof(directionText) - 1);
    } else if (currentAngle < -kDirectionDeg) {
        strncpy(directionText, "BACKWARD", sizeof(directionText) - 1);
    } else {
        strncpy(directionText, "STRAIGHT", sizeof(directionText) - 1);
    }
    directionText[sizeof(directionText) - 1] = '\0';

    const char* baseText =
        (currentAngle > kBadPostureDeg || currentAngle < -kBadPostureDeg) ? "BAD POSTURE"
                                                                          : "GOOD POSTURE";

    if (currentMode == MODE_TRAINING && isTrainingSessionActive()) {
        snprintf(postureText, sizeof(postureText),
                 "%s [#%lu %lus %lu-bad]", baseText,
                 (unsigned long)getTrainingSessionNumber(),
                 (unsigned long)getTrainingSessionDurationSec(),
                 (unsigned long)getTrainingSessionBadPostureCount());
    } else {
        strncpy(postureText, baseText, sizeof(postureText) - 1);
        postureText[sizeof(postureText) - 1] = '\0';
    }

    const uint32_t nowMs = millis();

    /* ESP32: bad if angle > 25 only; good after stable <= 25 for > 100 ms */
    if (currentAngle > kBadPostureDeg) {
        isBadPosture             = true;
        s_goodPostureStableStart = 0;
    } else {
        if (s_goodPostureStableStart == 0) {
            s_goodPostureStableStart = nowMs;
        }
        if ((nowMs - s_goodPostureStableStart) > kGoodDebounceMs) {
            isBadPosture = false;
        }
    }

    s_forwardMotorBad = isBadPosture;

    if (currentMode == MODE_TRAINING && isTrainingSessionActive()) {
        if (isBadPosture && !s_wasBadPosture) {
            s_badPostureCount++;
        }
    }
    s_wasBadPosture = isBadPosture;
}

static void logTrainingSensorRtt(uint32_t now) {
    if (now - s_lastSensorRttMs < 1000UL) return;
    s_lastSensorRttMs = now;

    if (!sensorInitialized) {
        rtt.println("[Training] LIS3DH not connected — check I2C (SDA P0.26 / SCL P0.27)");
        return;
    }

    rtt.print("[Training] raw m/s² X=");
    rtt.print(rawX, 2);
    rtt.print(" Y=");
    rtt.print(rawY, 2);
    rtt.print(" Z=");
    rtt.print(rawZ, 2);
    rtt.print(" | angle=");
    rtt.print(currentAngle, 1);
    rtt.print("° ");
    rtt.print(orientationText);
    rtt.print(" ");
    rtt.print(directionText);
    rtt.print(" moving=");
    rtt.print(_moving ? "Y" : "N");
    rtt.print(" | ");
    rtt.print(postureText);
    rtt.print(" | sub=");
    rtt.println(trainingSubModes[trainingSubModeIndex]);
}

/**
 * Instant: pulse after 200 ms sustained forward-bad.
 * Delayed: pulse after 5 s.
 * No alerts: motor off.
 */
static void applyTrainingMotorFeedback(uint32_t now) {
    if (trainingSubModeIndex >= TRAINING_SUBMODE_COUNT) {
        trainingSubModeIndex = 0;
    }

    if (trainingSubModeIndex == 2) {
        motorSetDuty(0);
        s_badMotorStartMs = 0;
        return;
    }

    if (!s_forwardMotorBad) {
        motorSetDuty(0);
        s_badMotorStartMs = 0;
        return;
    }

    if (s_badMotorStartMs == 0) {
        s_badMotorStartMs = now;
    }

    const unsigned long delayMs =
        (trainingSubModeIndex == 0) ? 200UL : 5000UL;
    if ((now - s_badMotorStartMs) < delayMs) {
        motorSetDuty(0);
        return;
    }

    const unsigned long vibInterval = 500UL;
    if ((now - s_vibToggleMs) >= vibInterval) {
        s_vibToggleMs = now;
        s_vibOn       = !s_vibOn;
    }
    motorSetDuty(s_vibOn ? VIB_INTENSITY_MAX : 0);
}

void sleepPostureSensor() {
    if (sensorInitialized) {
        lis.setDataRate(LIS3DH_DATARATE_POWERDOWN);
    }
}

void wakePostureSensor() {
    if (!sensorInitialized) {
        initPostureSensor();
        return;
    }
    lis.setDataRate(LIS3DH_DATARATE_100_HZ);
}

bool isDeviceMoving() {
    return _moving;
}

bool isTrainingSessionActive() {
    return s_sessionStartMs != 0;
}

uint32_t getTrainingSessionNumber() {
    return s_sessionNumber;
}

uint32_t getTrainingSessionDurationSec() {
    if (s_sessionStartMs == 0) return 0;
    return (millis() - s_sessionStartMs) / 1000UL;
}

uint32_t getTrainingSessionBadPostureCount() {
    return s_badPostureCount;
}

void trainingStart() {
    s_sessionStartMs  = millis();
    s_sessionNumber++;
    s_badPostureCount = 0;
    s_wasBadPosture   = false;
    rtt.println("Training: start");
}

void trainingStop() {
    s_sessionStartMs = 0;
    motorSetDuty(0);
    s_badMotorStartMs = 0;
    s_vibOn           = false;
    s_forwardMotorBad = false;
    rtt.println("Training: stop");
}

void trainingSetup() {
    initPostureSensor();
}

void trainingLoop() {
    const uint32_t now = millis();

    static bool s_prevCalibrating = false;
    const bool  calibrating      = isCalibrating();
    if (s_prevCalibrating && !calibrating) {
        s_badMotorStartMs = 0;
        s_vibToggleMs     = 0;
        s_vibOn           = false;
    }
    s_prevCalibrating = calibrating;

    if (calibrating) {
        return;
    }

    if (currentMode == MODE_TRAINING) {
        if (s_lastModeForSession != MODE_TRAINING) {
            wakePostureSensor();
            trainingStart();
            s_lastSensorRttMs = 0;
            s_lastModeForSession = MODE_TRAINING;
        }
        updatePostureAngle();
        applyTrainingMotorFeedback(now);
        logTrainingSensorRtt(now);
    } else {
        if (s_lastModeForSession == MODE_TRAINING) {
            trainingStop();
        }
        s_lastModeForSession = currentMode;
    }
}

