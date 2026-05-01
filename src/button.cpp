#include "button.h"
#include "calibration.h"
#include "therapy.h"
#include "storage.h"

extern RTTStream rtt;

// ── Name arrays ────────────────────────────────────────────────────────────
const char* modeNames[]        = { "Training Mode", "Therapy Mode", "OFF Mode" };
const char* trainingSubModes[] = { "Instant", "Delayed", "No alerts" };
const char* therapySubModes[]  = { "10 min", "20 min", "30 min" };

// ── State definitions ──────────────────────────────────────────────────────
bool    deviceOn             = true;
Mode    currentMode          = MODE_TRAINING;
uint8_t trainingSubModeIndex = 0;
uint8_t therapySubModeIndex  = 0;

// ── LED (P0.15 → Arduino 4, common-anode: LOW = on) ────────────────────────
#define PIN_BLINK_LED  PIN_LED_BLUE
#define LED_ON()       digitalWrite(PIN_BLINK_LED, LOW)
#define LED_OFF()      digitalWrite(PIN_BLINK_LED, HIGH)

// Debounced button: one clean edge only after raw stable for DEBOUNCE_MS.
static int      btnLastRaw        = HIGH;
static uint32_t btnLastDebounceMs = 0;
static bool     btnStablePressed  = false;  // true = electrical LOW (pressed)

static uint32_t tapPressStartMs   = 0;
static bool     tapPendingSecond  = false;
static uint32_t tapFirstMs        = 0;
static bool     holdConsumedTap   = false;
static uint32_t lastAcceptedClickMs = 0;

enum ButtonEvent { EVT_NONE, EVT_SINGLE, EVT_DOUBLE, EVT_HOLD };

static void printCurrentMode() {
    rtt.print("Mode: ");
    rtt.println(modeNames[currentMode]);
}

static void handleSingleClick() {
    uint32_t now = millis();
    if ((now - lastAcceptedClickMs) < 350) {
        return;
    }
    lastAcceptedClickMs = now;

    rtt.println("Single click");
    switch (currentMode) {
        case MODE_TRAINING:
            rtt.println("Switch -> Therapy");
            currentMode = MODE_THERAPY;
            printCurrentMode();
            rtt.print("Therapy Sub-Mode: ");
            rtt.println(therapySubModes[therapySubModeIndex]);
            therapyStart();
            break;

        case MODE_THERAPY:
            rtt.println("Switch -> OFF");
            if (therapyIsRunning()) {
                therapyStop(false);
            }
            currentMode = MODE_OFF;
            printCurrentMode();
            break;

        case MODE_OFF:
            rtt.println("Switch -> Training");
            currentMode = MODE_TRAINING;
            printCurrentMode();
            break;

        default:
            break;
    }
}

static void handleDoubleClick() {
    rtt.println("Double click");

    switch (currentMode) {
        case MODE_TRAINING:
            trainingSubModeIndex = (trainingSubModeIndex + 1) % TRAINING_SUBMODE_COUNT;
            rtt.print("Training Sub-Mode: ");
            rtt.println(trainingSubModes[trainingSubModeIndex]);
            break;

        case MODE_THERAPY:
            // Stop session but stay in Therapy mode, then apply new duration
            therapyStop(false);
            therapySubModeIndex = (therapySubModeIndex + 1) % THERAPY_SUBMODE_COUNT;
            storageSaveTherapySubMode(therapySubModeIndex);
            rtt.print("Therapy Sub-Mode changed: ");
            rtt.println(therapySubModes[therapySubModeIndex]);
            therapyStart();
            break;

        case MODE_OFF:
        default:
            break;
    }
}

static void handleHold() {
    rtt.println("Hold");
    if (isCalibrating()) {
        calibrationRequestCancel();
    } else {
        calibrationRequestStart();
    }
}

/** Emit pending single click after double-click window expired. */
static ButtonEvent maybeEmitPendingSingle(uint32_t now) {
    if (tapPendingSecond && (now - tapFirstMs) > DOUBLE_CLICK_GAP_MS) {
        tapPendingSecond = false;
        return EVT_SINGLE;
    }
    return EVT_NONE;
}

static ButtonEvent pollButton() {
    uint32_t now = millis();
    int      raw  = digitalRead(PIN_BUTTON);

    if (raw != btnLastRaw) {
        btnLastDebounceMs = now;
        btnLastRaw        = raw;
    }

    if ((now - btnLastDebounceMs) < DEBOUNCE_MS) {
        return maybeEmitPendingSingle(now);
    }

    bool pressed = (raw == LOW);

    // Stable transition after debounce
    if (pressed != btnStablePressed) {
        btnStablePressed = pressed;

        if (btnStablePressed) {
            tapPressStartMs = now;
            holdConsumedTap = false;
            LED_ON();
        } else {
            LED_OFF();
            if (!holdConsumedTap) {
                uint32_t dur = now - tapPressStartMs;
                // Real button press, not chatter; not a hold
                if (dur >= 80 && dur < HOLD_MS) {
                    if (tapPendingSecond && (now - tapFirstMs) <= DOUBLE_CLICK_GAP_MS) {
                        tapPendingSecond = false;
                        return EVT_DOUBLE;
                    }
                    tapPendingSecond = true;
                    tapFirstMs       = now;
                }
            }
            holdConsumedTap = false;
        }
    }

    if (btnStablePressed && !holdConsumedTap && (now - tapPressStartMs) >= HOLD_MS) {
        holdConsumedTap   = true;
        tapPendingSecond  = false;
        return EVT_HOLD;
    }

    return maybeEmitPendingSingle(now);
}

void buttonSetup() {
    pinMode(PIN_BUTTON,    INPUT_PULLUP);
    pinMode(PIN_BLINK_LED, OUTPUT);
    LED_OFF();

    therapySubModeIndex = storageLoadTherapySubMode();
    lastAcceptedClickMs = 0;
    btnLastRaw          = digitalRead(PIN_BUTTON);
    btnLastDebounceMs   = millis();
    btnStablePressed    = (btnLastRaw == LOW);
    tapPendingSecond    = false;
    holdConsumedTap     = false;

    rtt.println("Device ON");
    currentMode = MODE_TRAINING;
    printCurrentMode();
}

void buttonLoop() {
    ButtonEvent evt = pollButton();

    switch (evt) {
        case EVT_SINGLE: handleSingleClick(); break;
        case EVT_DOUBLE: handleDoubleClick(); break;
        case EVT_HOLD:   handleHold();        break;
        default: break;
    }

    if (currentMode == MODE_THERAPY && !therapyIsRunning() && !isCalibrating()) {
        rtt.println("Therapy auto-start (mode is Therapy)");
        therapyStart();
    }
}
