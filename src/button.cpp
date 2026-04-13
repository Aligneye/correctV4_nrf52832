#include "button.h"
#include "nrf.h"
#include "calibration.h"
#include "therapy.h"
#include "storage.h"

extern RTTStream rtt;

// ── Name arrays ────────────────────────────────────────────────────────────
const char* modeNames[]        = { "Tracking Mode", "Training Mode", "Therapy Mode" };
const char* trainingSubModes[] = { "Instant", "Delayed" };
const char* trackingSubModes[] = { "Off" };
const char* therapySubModes[]  = { "5 min", "10 min", "20 min" };

// ── State definitions ──────────────────────────────────────────────────────
bool    deviceOn             = true;
Mode    currentMode          = MODE_TRACKING;
uint8_t trainingSubModeIndex = 0;
uint8_t trackingSubModeIndex = 0;
uint8_t therapySubModeIndex  = 0;

// ── LED (P0.15 → Arduino 4, common-anode: LOW = on) ────────────────────────
#define PIN_BLINK_LED  PIN_LED_BLUE
#define LED_ON()       digitalWrite(PIN_BLINK_LED, LOW)
#define LED_OFF()      digitalWrite(PIN_BLINK_LED, HIGH)

// ── Internal button state ──────────────────────────────────────────────────
static bool     lastRawState       = HIGH;
static bool     stableState        = HIGH;
static uint32_t debounceTimestamp  = 0;

static bool     waitingSecondClick = false;
static uint32_t firstClickTime     = 0;
static uint8_t  clickCount         = 0;

static bool     holdFired          = false;
static uint32_t pressStartTime     = 0;

// ── Sleep using System ON + WFI ────────────────────────────────────────────
#define SLEEP_GPIOTE_CH  0

extern "C" void GPIOTE_IRQHandler(void) {
    NRF_GPIOTE->EVENTS_IN[SLEEP_GPIOTE_CH] = 0;
    (void)NRF_GPIOTE->EVENTS_IN[SLEEP_GPIOTE_CH];
}

static void enterSleep() {
    rtt.println("Device OFF — sleeping");
    delay(10);

    uint32_t nrfPin = g_ADigitalPinMap[PIN_BUTTON];
    NRF_GPIOTE->CONFIG[SLEEP_GPIOTE_CH] =
        (GPIOTE_CONFIG_MODE_Event      << GPIOTE_CONFIG_MODE_Pos)     |
        (nrfPin                        << GPIOTE_CONFIG_PSEL_Pos)     |
        (GPIOTE_CONFIG_POLARITY_HiToLo << GPIOTE_CONFIG_POLARITY_Pos);

    NRF_GPIOTE->EVENTS_IN[SLEEP_GPIOTE_CH] = 0;
    (void)NRF_GPIOTE->EVENTS_IN[SLEEP_GPIOTE_CH];

    NRF_GPIOTE->INTENSET = (1UL << SLEEP_GPIOTE_CH);
    NVIC_ClearPendingIRQ(GPIOTE_IRQn);
    NVIC_EnableIRQ(GPIOTE_IRQn);

    __DSB();
    __WFI();

    NRF_GPIOTE->INTENCLR = (1UL << SLEEP_GPIOTE_CH);
    NVIC_DisableIRQ(GPIOTE_IRQn);
    NRF_GPIOTE->CONFIG[SLEEP_GPIOTE_CH] =
        (GPIOTE_CONFIG_MODE_Disabled << GPIOTE_CONFIG_MODE_Pos);

    uint32_t waitStart = millis();
    while (digitalRead(PIN_BUTTON) == LOW) {
        if ((millis() - waitStart) > 3000) break;
    }
    delay(50);

    lastRawState       = HIGH;
    stableState        = HIGH;
    debounceTimestamp  = millis();
    waitingSecondClick = false;
    clickCount         = 0;
    holdFired          = false;
    LED_OFF();

    deviceOn    = true;
    currentMode = MODE_TRACKING;
    rtt.println("Device ON");
    rtt.print("Mode: ");
    rtt.println(modeNames[currentMode]);
}

// ── Internal helpers ───────────────────────────────────────────────────────
enum ButtonEvent { EVT_NONE, EVT_SINGLE, EVT_DOUBLE, EVT_HOLD };

static void printCurrentMode() {
    rtt.print("Mode: ");
    rtt.println(modeNames[currentMode]);
}

static void handleSingleClick() {
    rtt.println("Single click");
    if (therapyIsRunning()) {
        // therapyStop() sets currentMode to MODE_TRAINING automatically
        therapyStop();
        printCurrentMode();
        return;
    }
    currentMode = (Mode)((currentMode + 1) % MODE_COUNT);
    printCurrentMode();
    if (currentMode == MODE_THERAPY) {
        therapyStart();
    }
}

static void handleDoubleClick() {
    rtt.println("Double click");
    if (currentMode == MODE_TRACKING) {
        enterSleep();
        return;
    }

    switch (currentMode) {
        case MODE_TRAINING:
            trainingSubModeIndex = (trainingSubModeIndex + 1) % TRAINING_SUBMODE_COUNT;
            rtt.print("Training Sub-Mode: ");
            rtt.println(trainingSubModes[trainingSubModeIndex]);
            break;

        case MODE_THERAPY:
            // Double-click cycles duration and restarts therapy
            if (therapyIsRunning()) {
                therapyStop();
            }
            therapySubModeIndex = (therapySubModeIndex + 1) % THERAPY_SUBMODE_COUNT;
            storageSaveTherapySubMode(therapySubModeIndex);
            rtt.print("Therapy Sub-Mode changed: ");
            rtt.println(therapySubModes[therapySubModeIndex]);
            therapyStart();
            break;

        default:
            break;
    }
}

static void handleHold() {
    rtt.println("Hold");
    if (calibrationIsActive()) {
        calibrationStop();
    } else {
        calibrationStart();
    }
}

static ButtonEvent pollButton() {
    uint32_t now      = millis();
    bool     rawState = digitalRead(PIN_BUTTON);

    if (rawState != lastRawState) {
        debounceTimestamp = now;
        lastRawState      = rawState;
    }

    if ((now - debounceTimestamp) < DEBOUNCE_MS) {
        if (waitingSecondClick && (now - firstClickTime) > DOUBLE_CLICK_GAP_MS) {
            waitingSecondClick = false;
            clickCount         = 0;
            return EVT_SINGLE;
        }
        return EVT_NONE;
    }

    bool pressed = (rawState == LOW);

    if (pressed && stableState == HIGH) {
        stableState    = LOW;
        pressStartTime = now;
        holdFired      = false;
        LED_ON();   // light P0.15 while pressed (held = stays on)
    }

    if (pressed && stableState == LOW && !holdFired) {
        if ((now - pressStartTime) >= HOLD_MS) {
            holdFired          = true;
            waitingSecondClick = false;
            clickCount         = 0;
            return EVT_HOLD;
        }
    }

    if (!pressed && stableState == LOW) {
        stableState = HIGH;
        LED_OFF();   // release → LED off

        if (!holdFired) {
            clickCount++;
            if (clickCount == 1) {
                waitingSecondClick = true;
                firstClickTime     = now;
            } else if (clickCount >= 2) {
                waitingSecondClick = false;
                clickCount         = 0;
                return EVT_DOUBLE;
            }
        }
        holdFired = false;
    }

    if (waitingSecondClick && (now - firstClickTime) > DOUBLE_CLICK_GAP_MS) {
        waitingSecondClick = false;
        clickCount         = 0;
        return EVT_SINGLE;
    }

    return EVT_NONE;
}

// ── Public API ─────────────────────────────────────────────────────────────
void buttonSetup() {
    pinMode(PIN_BUTTON,    INPUT_PULLUP);
    pinMode(PIN_BLINK_LED, OUTPUT);
    LED_OFF();   // idle off (common anode)

    therapySubModeIndex = storageLoadTherapySubMode();

    rtt.println("Device ON");
    currentMode = MODE_TRACKING;
    printCurrentMode();
    rtt.print("Therapy Sub-Mode: ");
    rtt.println(therapySubModes[therapySubModeIndex]);
}

void buttonLoop() {
    ButtonEvent evt = pollButton();

    switch (evt) {
        case EVT_SINGLE: handleSingleClick(); break;
        case EVT_DOUBLE: handleDoubleClick(); break;
        case EVT_HOLD:   handleHold();        break;
        default: break;
    }
}
