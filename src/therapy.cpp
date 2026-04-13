#include "therapy.h"
#include "button.h"
#include "motor.h"
#include <math.h>

extern RTTStream rtt;

// ── Pattern names ──────────────────────────────────────────────────────────
static const char* PATTERN_NAMES[] = {
    "Muscle Act",   // 0
    "Rev Ramp",     // 1
    "Ramp",         // 2
    "Wave",         // 3
    "Slow Wave",    // 4
    "Sine Wave",    // 5
    "Triangle",     // 6
    "Dbl Wave",     // 7
    "Anti-Fatigue", // 8
    "Pulse Ramp"    // 9
};

// ── Session state ──────────────────────────────────────────────────────────
static TherapyState  therapyState        = THERAPY_IDLE;
static unsigned long therapyStartMs      = 0;
static unsigned long therapyDurationMs   = THERAPY_DURATION_10_MIN;

// patternDurations[i] holds the randomly chosen duration for each pattern slot
// Max slots needed: 20 min = 1200s, with 60s min per pattern → max 20 slots
#define MAX_THERAPY_PATTERNS 20
static int           patternSequence[MAX_THERAPY_PATTERNS];
static unsigned long patternDurations[MAX_THERAPY_PATTERNS];
static int           totalPatterns       = 0;
static int           currentPatternIndex = 0;
static unsigned long patternStartMs      = 0;

// ── RTT ticker — prints status every 1 s ──────────────────────────────────
static unsigned long lastTickMs = 0;

// ── Duration lookup from button sub-mode ──────────────────────────────────
static unsigned long durationForSubMode(uint8_t idx) {
    switch (idx) {
        case 0: return THERAPY_DURATION_5_MIN;
        case 2: return THERAPY_DURATION_20_MIN;
        default: return THERAPY_DURATION_10_MIN;
    }
}

// ── Fisher-Yates shuffle ───────────────────────────────────────────────────
static void shuffleArr(int* arr, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = random(0, i + 1);
        int tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
    }
}

// ── Build pattern sequence: all patterns = 1 min each ─────────────────────
static void initializePatternSequence() {
    currentPatternIndex = 0;

    // Determine number of patterns based on selected duration
    int minutes = therapyDurationMs / 60000UL;
    totalPatterns = minutes;  // 5 min → 5 patterns, 10 → 10, 20 → 20

    // Safety: cap at max array size
    if (totalPatterns > MAX_THERAPY_PATTERNS) {
        rtt.println("[ERROR] Too many patterns! Capping at 20.");
        totalPatterns = MAX_THERAPY_PATTERNS;
    }

    // First pattern is always Muscle Activation, 1 min
    patternSequence[0]  = PATTERN_MUSCLE_ACTIVATION;
    patternDurations[0] = 60000UL;

    // Build pool of remaining 9 patterns (all except Muscle Activation)
    int patternPool[PATTERN_COUNT - 1];
    int poolSize = 0;
    for (int i = PATTERN_REVERSE_RAMP; i < PATTERN_COUNT; i++) {
        patternPool[poolSize++] = i;
    }

    // Shuffle the pool
    shuffleArr(patternPool, poolSize);

    // Fill remaining slots: try to use unique patterns first, then repeat if needed
    for (int i = 1; i < totalPatterns; i++) {
        // Use patterns from shuffled pool, wrap around if we need more than 9
        int poolIndex = (i - 1) % poolSize;

        // If we've exhausted the pool and need to repeat, reshuffle for variety
        if (i > 1 && poolIndex == 0) {
            shuffleArr(patternPool, poolSize);
        }

        patternSequence[i]  = patternPool[poolIndex];
        patternDurations[i] = 60000UL;  // All patterns = 1 min
    }

    // ── Print full session plan ────────────────────────────────────────────
    rtt.println("=== Therapy Session Plan ===");
    rtt.print("Session: ");
    rtt.print(minutes);
    rtt.print(" min → ");
    rtt.print(totalPatterns);
    rtt.println(" patterns (1 min each)");

    for (int i = 0; i < totalPatterns; i++) {
        rtt.print("  [");
        rtt.print(i + 1);
        rtt.print("] ");
        rtt.println(PATTERN_NAMES[patternSequence[i]]);
    }
    rtt.println("============================");
}

// ── Per-second RTT status line ─────────────────────────────────────────────
static void printTick(unsigned long now) {
    if (now - lastTickMs < 1000UL) return;
    lastTickMs = now;

    unsigned long elapsed      = now - therapyStartMs;
    unsigned long patElapsed   = now - patternStartMs;
    unsigned long patRemain    = (patElapsed < 60000UL) ? 60000UL - patElapsed : 0;

    unsigned long totalElapsedS = elapsed / 1000UL;
    unsigned long patElapsedS   = patElapsed / 1000UL;
    unsigned long patRemainS    = patRemain / 1000UL;

    rtt.print("[Therapy] ");
    rtt.print(totalElapsedS);
    rtt.print("s | Pattern ");
    rtt.print(currentPatternIndex + 1);
    rtt.print("/");
    rtt.print(totalPatterns);
    rtt.print(" [");
    rtt.print(PATTERN_NAMES[patternSequence[currentPatternIndex]]);
    rtt.print("] ");
    rtt.print(patElapsedS);
    rtt.print("s/60s | ");
    rtt.print(patRemainS);
    rtt.println("s left");
}

// ── Individual therapy patterns ────────────────────────────────────────────

// Muscle Activation — 30% → 100% ramp over 60s
static void patternMuscleActivation(unsigned long e) {
    if (e <= 60000UL) {
        float pct = 30.0f + ((float)e / 60000.0f) * 70.0f;
        motorSetDuty(constrain((int)((pct / 100.0f) * 255.0f), 0, 255));
    } else {
        motorSetDuty(0);
    }
}

// Reverse Ramp — 100% → 30% over 60s
static void patternReverseRamp(unsigned long e) {
    if (e <= 60000UL) {
        float pct = 100.0f - ((float)e / 60000.0f) * 70.0f;
        motorSetDuty(constrain((int)((pct / 100.0f) * 255.0f), 0, 255));
    } else {
        motorSetDuty(0);
    }
}

// Ramp up/down triangle (~10.2 s cycle, independent of dur)
static void patternRampPattern(unsigned long e) {
    unsigned long c = e % 10200UL;
    int pwm = (c < 5100UL)
        ? map(c, 0, 5100, 0, 255)
        : map(c, 5100, 10200, 255, 0);
    motorSetDuty(constrain(pwm, 0, 255));
}

// Wave therapy — short/long on-off bursts, 2.4 s cycle
static void patternWaveTherapy(unsigned long e) {
    unsigned long c = e % 2400UL;
    int v;
    if      (c < 100)  v = 255;
    else if (c < 400)  v = 0;
    else if (c < 900)  v = 255;
    else if (c < 1200) v = 0;
    else if (c < 2200) v = 255;
    else               v = 0;
    motorSetDuty(v);
}

// Slow wave — 50 ↔ 200 over 18 s cycle
static void patternSlowWave(unsigned long e) {
    unsigned long c = e % 18000UL;
    int pwm = (c < 9000UL)
        ? map(c, 0, 9000, 50, 200)
        : map(c, 9000, 18000, 200, 50);
    motorSetDuty(constrain(pwm, 0, 255));
}

// Sinusoidal — 50-200, 2 s period
static void patternSinusoidalWave(unsigned long e) {
    float rad = (e / 1000.0f) * 360.0f / 2.0f * (float)PI / 180.0f;
    float s   = (sinf(rad) + 1.0f) * 0.5f;
    motorSetDuty(constrain((int)(50.0f + s * 150.0f), 0, 255));
}

// Triangle — 60 ↔ 200, 4 s cycle
static void patternTriangleWave(unsigned long e) {
    unsigned long c = e % 4000UL;
    int pwm = (c < 2000UL)
        ? map(c, 0, 2000, 60, 200)
        : map(c, 2000, 4000, 200, 60);
    motorSetDuty(constrain(pwm, 0, 255));
}

// Double wave — slow carrier + fast ripple
static void patternDoubleWave(unsigned long e) {
    float t       = e / 1000.0f;
    float slow    = (sinf(t * 360.0f / 4.0f * (float)PI / 180.0f) + 1.0f) * 0.5f;
    int   base    = (int)(70.0f + slow * 185.0f);
    int   ripple  = (int)(30.0f * sinf(t * 360.0f / 0.5f * (float)PI / 180.0f));
    motorSetDuty(constrain(base + ripple, 0, 255));
}

// Anti-fatigue — pulse train + rest, 5 s cycle
static void patternAntiFatigue(unsigned long e) {
    unsigned long c = e % 5000UL;
    if (c < 4500UL) {
        motorSetDuty((c % 500UL < 200UL) ? 225 : 0);
    } else {
        motorSetDuty(0);
    }
}

// Pulse ramp — stepped recovery, 9 s cycle
static void patternPulseRamp(unsigned long e) {
    static const int intensities[9] = { 120,150,180,210,240,210,180,150,120 };
    unsigned long c  = e % 9000UL;
    int step         = (c / 1000UL) % 9;
    motorSetDuty((c % 1000UL < 200UL) ? intensities[step] : 0);
}

// ── Pattern dispatcher ─────────────────────────────────────────────────────
static void executePattern(int idx, unsigned long elapsed) {
    switch (idx) {
        case PATTERN_MUSCLE_ACTIVATION: patternMuscleActivation(elapsed); break;
        case PATTERN_REVERSE_RAMP:      patternReverseRamp(elapsed);      break;
        case PATTERN_RAMP_PATTERN:      patternRampPattern(elapsed);      break;
        case PATTERN_WAVE_THERAPY:      patternWaveTherapy(elapsed);      break;
        case PATTERN_SLOW_WAVE:         patternSlowWave(elapsed);         break;
        case PATTERN_SINUSOIDAL_WAVE:   patternSinusoidalWave(elapsed);   break;
        case PATTERN_TRIANGLE_WAVE:     patternTriangleWave(elapsed);     break;
        case PATTERN_DOUBLE_WAVE:       patternDoubleWave(elapsed);       break;
        case PATTERN_ANTI_FATIGUE:      patternAntiFatigue(elapsed);      break;
        case PATTERN_PULSE_RAMP:        patternPulseRamp(elapsed);        break;
        default:                        motorSetDuty(0);                  break;
    }
}

// ── Public API ─────────────────────────────────────────────────────────────
void therapySetup() {
    motorSetup();
    therapyState = THERAPY_IDLE;
    rtt.println("Therapy module ready");
}

void therapyStart() {
    therapyDurationMs   = durationForSubMode(therapySubModeIndex);
    therapyStartMs      = millis();
    patternStartMs      = therapyStartMs;
    lastTickMs          = therapyStartMs;
    therapyState        = THERAPY_RUNNING;
    initializePatternSequence();

    rtt.print("Therapy: starting — sub-mode ");
    rtt.print((int)therapySubModeIndex);
    rtt.print(" (");
    rtt.print((unsigned long)(therapyDurationMs / 60000UL));
    rtt.println(" min session)");
    rtt.print("First pattern: ");
    rtt.println(PATTERN_NAMES[patternSequence[0]]);
}

void therapyStop() {
    motorSetDuty(0);
    therapyState = THERAPY_IDLE;
    rtt.println("Therapy: session complete — switching to Training mode");

    // Switch to training mode automatically
    currentMode = MODE_TRAINING;
    rtt.print("Mode: ");
    rtt.println(modeNames[currentMode]);
}

void therapyLoop() {
    if (therapyState != THERAPY_RUNNING) return;

    unsigned long now            = millis();
    unsigned long patternElapsed = now - patternStartMs;

    // ── Advance to next pattern after 60 seconds ─────────────────────────
    if (patternElapsed >= 60000UL) {
        currentPatternIndex++;
        patternStartMs   = now;
        patternElapsed   = 0;

        if (currentPatternIndex >= totalPatterns) {
            therapyStop();
            return;
        }

        rtt.println("----------------------------------------");
        rtt.print("[Pattern ");
        rtt.print(currentPatternIndex + 1);
        rtt.print("/");
        rtt.print(totalPatterns);
        rtt.print("] ");
        rtt.println(PATTERN_NAMES[patternSequence[currentPatternIndex]]);
        if (currentPatternIndex + 1 < totalPatterns) {
            rtt.print("  Next: ");
            rtt.println(PATTERN_NAMES[patternSequence[currentPatternIndex + 1]]);
        } else {
            rtt.println("  (Last pattern)");
        }
        rtt.println("----------------------------------------");
    }

    // ── Per-second status tick ────────────────────────────────────────────
    printTick(now);

    // ── Run current pattern ───────────────────────────────────────────────
    executePattern(patternSequence[currentPatternIndex], patternElapsed);
}

bool therapyIsRunning() {
    return therapyState == THERAPY_RUNNING;
}

unsigned long therapyGetElapsedMs() {
    if (therapyState != THERAPY_RUNNING) return 0;
    return millis() - therapyStartMs;
}

unsigned long therapyGetRemainingMs() {
    if (therapyState != THERAPY_RUNNING) return 0;
    // All patterns are 60s each
    unsigned long patElapsed = millis() - patternStartMs;
    unsigned long currentPatRemain = (patElapsed < 60000UL) ? (60000UL - patElapsed) : 0;
    unsigned long futurePatterns = (totalPatterns - currentPatternIndex - 1) * 60000UL;
    return currentPatRemain + futurePatterns;
}

const char* therapyGetCurrentPatternName() {
    if (therapyState != THERAPY_RUNNING || currentPatternIndex >= totalPatterns) {
        return "Idle";
    }
    return PATTERN_NAMES[patternSequence[currentPatternIndex]];
}

const char* therapyGetNextPatternName() {
    if (therapyState != THERAPY_RUNNING ||
        currentPatternIndex + 1 >= totalPatterns) {
        return "Complete";
    }
    return PATTERN_NAMES[patternSequence[currentPatternIndex + 1]];
}
