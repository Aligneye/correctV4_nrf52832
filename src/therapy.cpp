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

// ── Per-pattern duration options (ms): 1 min / 1.5 min / 2 min ────────────
static const unsigned long PATTERN_DURATIONS[] = { 60000UL, 90000UL, 120000UL };
#define PATTERN_DURATION_COUNT 3

// ── Session state ──────────────────────────────────────────────────────────
static TherapyState  therapyState        = THERAPY_IDLE;
static unsigned long therapyStartMs      = 0;
static unsigned long therapyDurationMs   = THERAPY_DURATION_10_MIN;

// patternDurations[i] holds the randomly chosen duration for each pattern slot
static int           patternSequence[PATTERN_COUNT];
static unsigned long patternDurations[PATTERN_COUNT];
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

// ── Build pattern sequence + per-pattern random durations ─────────────────
static void initializePatternSequence() {
    currentPatternIndex = 0;

    // First pattern is always Muscle Activation, fixed 1 min (warm-up)
    patternSequence[0]  = PATTERN_MUSCLE_ACTIVATION;
    patternDurations[0] = 60000UL;

    int minutes = therapyDurationMs / 60000UL;
    int slots;
    if      (minutes == 5)  slots = 3;
    else if (minutes == 20) slots = 10;
    else                    slots = 5;   // 10 min default

    totalPatterns = slots;

    // Build shuffled pool of remaining 9 patterns
    int remaining[PATTERN_COUNT - 1];
    int count = 0;
    for (int i = PATTERN_REVERSE_RAMP; i < PATTERN_COUNT; i++) {
        remaining[count++] = i;
    }
    shuffleArr(remaining, count);

    // Fill remaining slots with random pattern + random duration
    for (int i = 1; i < slots; i++) {
        patternSequence[i]  = remaining[i - 1];
        patternDurations[i] = PATTERN_DURATIONS[random(0, PATTERN_DURATION_COUNT)];
    }

    // ── Print full session plan ────────────────────────────────────────────
    rtt.println("=== Therapy Session Plan ===");
    rtt.print("Total patterns: "); rtt.println(slots);
    unsigned long totalMs = 0;
    for (int i = 0; i < slots; i++) {
        totalMs += patternDurations[i];
        rtt.print("  ["); rtt.print(i + 1); rtt.print("] ");
        rtt.print(PATTERN_NAMES[patternSequence[i]]);
        rtt.print(" — ");
        rtt.print((unsigned long)(patternDurations[i] / 1000UL));
        rtt.println(" s");
    }
    rtt.print("Estimated total: ");
    rtt.print((unsigned long)(totalMs / 1000UL));
    rtt.println(" s");
    rtt.println("============================");
}

// ── Per-second RTT status line ─────────────────────────────────────────────
static void printTick(unsigned long now) {
    if (now - lastTickMs < 1000UL) return;
    lastTickMs = now;

    unsigned long elapsed   = now - therapyStartMs;
    unsigned long patElapsed = now - patternStartMs;
    unsigned long patRemain  = (patElapsed < patternDurations[currentPatternIndex])
                                ? patternDurations[currentPatternIndex] - patElapsed
                                : 0;

    unsigned long totalElapsedS  = elapsed / 1000UL;
    unsigned long patElapsedS    = patElapsed / 1000UL;
    unsigned long patRemainS     = patRemain / 1000UL;

    rtt.print("[Therapy] ");
    rtt.print(totalElapsedS);
    rtt.print("s elapsed | Pattern ");
    rtt.print(currentPatternIndex + 1);
    rtt.print("/");
    rtt.print(totalPatterns);
    rtt.print(" [");
    rtt.print(PATTERN_NAMES[patternSequence[currentPatternIndex]]);
    rtt.print("] ");
    rtt.print(patElapsedS);
    rtt.print("s / ");
    rtt.print(patternDurations[currentPatternIndex] / 1000UL);
    rtt.print("s | remain ");
    rtt.print(patRemainS);
    rtt.println("s");
}

// ── Individual therapy patterns ────────────────────────────────────────────

// Muscle Activation — 30% → 100% ramp over its allotted duration
static void patternMuscleActivation(unsigned long e, unsigned long dur) {
    if (e <= dur) {
        float pct = 30.0f + ((float)e / (float)dur) * 70.0f;
        motorSetDuty(constrain((int)((pct / 100.0f) * 255.0f), 0, 255));
    } else {
        motorSetDuty(0);
    }
}

// Reverse Ramp — 100% → 30%
static void patternReverseRamp(unsigned long e, unsigned long dur) {
    if (e <= dur) {
        float pct = 100.0f - ((float)e / (float)dur) * 70.0f;
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
static void executePattern(int idx, unsigned long elapsed, unsigned long dur) {
    switch (idx) {
        case PATTERN_MUSCLE_ACTIVATION: patternMuscleActivation(elapsed, dur); break;
        case PATTERN_REVERSE_RAMP:      patternReverseRamp(elapsed, dur);      break;
        case PATTERN_RAMP_PATTERN:      patternRampPattern(elapsed);            break;
        case PATTERN_WAVE_THERAPY:      patternWaveTherapy(elapsed);            break;
        case PATTERN_SLOW_WAVE:         patternSlowWave(elapsed);               break;
        case PATTERN_SINUSOIDAL_WAVE:   patternSinusoidalWave(elapsed);         break;
        case PATTERN_TRIANGLE_WAVE:     patternTriangleWave(elapsed);           break;
        case PATTERN_DOUBLE_WAVE:       patternDoubleWave(elapsed);             break;
        case PATTERN_ANTI_FATIGUE:      patternAntiFatigue(elapsed);            break;
        case PATTERN_PULSE_RAMP:        patternPulseRamp(elapsed);              break;
        default:                        motorSetDuty(0);                        break;
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

    unsigned long now          = millis();
    unsigned long patternElapsed = now - patternStartMs;
    unsigned long curDur         = patternDurations[currentPatternIndex];

    // ── Advance to next pattern when current duration elapsed ────────────
    if (patternElapsed >= curDur) {
        currentPatternIndex++;
        patternStartMs   = now;
        patternElapsed   = 0;

        if (currentPatternIndex >= totalPatterns) {
            therapyStop();
            return;
        }

        rtt.println("----------------------------------------");
        rtt.print("[Pattern change] -> ");
        rtt.print(PATTERN_NAMES[patternSequence[currentPatternIndex]]);
        rtt.print("  (");
        rtt.print((unsigned long)(patternDurations[currentPatternIndex] / 1000UL));
        rtt.println(" s)");
        if (currentPatternIndex + 1 < totalPatterns) {
            rtt.print("  Next up: ");
            rtt.println(PATTERN_NAMES[patternSequence[currentPatternIndex + 1]]);
        } else {
            rtt.println("  Next up: (last pattern)");
        }
        rtt.println("----------------------------------------");

        curDur = patternDurations[currentPatternIndex];
    }

    // ── Per-second status tick ────────────────────────────────────────────
    printTick(now);

    // ── Run current pattern ───────────────────────────────────────────────
    executePattern(patternSequence[currentPatternIndex], patternElapsed, curDur);
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
    // Sum remaining durations from current pattern onward
    unsigned long rem = 0;
    unsigned long patElapsed = millis() - patternStartMs;
    unsigned long curDur = patternDurations[currentPatternIndex];
    rem += (patElapsed < curDur) ? (curDur - patElapsed) : 0;
    for (int i = currentPatternIndex + 1; i < totalPatterns; i++) {
        rem += patternDurations[i];
    }
    return rem;
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
