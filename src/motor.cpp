#include "motor.h"
#include "config.h"
#include "nrf.h"

// ── Hardware timer PWM via TIMER1 ──────────────────────────────────────────
// TIMER1 is free on nRF52832 when no SoftDevice is present (TIMER0 is used
// by SD; TIMER1/2/3/4 are application-available). We configure TIMER1 in
// 32-bit mode at 1 MHz tick, using two CC channels:
//   CC[0] = ON time  (fires → pin HIGH)
//   CC[1] = period   (fires → pin LOW, reloads CC[0] for next cycle)
//
// PWM period = 10 ms (100 Hz) — slow enough that loop jitter is irrelevant,
// fast enough that a vibration motor feels it as continuous intensity.
//
// duty 0   → motor always off (timer not started)
// duty 255 → motor always on  (timer not started, pin held HIGH)
// 1–254    → timer runs, CC[0] = (duty * 10000) / 255 µs

static constexpr uint32_t TIMER_FREQ_HZ  = 1000000UL;  // 1 MHz prescaler
static constexpr uint32_t PWM_PERIOD_US  = 10000UL;     // 10 ms = 100 Hz

static volatile uint8_t g_duty = 0;

// ── TIMER1 IRQ ─────────────────────────────────────────────────────────────
extern "C" void TIMER1_IRQHandler(void) {
    if (NRF_TIMER1->EVENTS_COMPARE[0]) {
        NRF_TIMER1->EVENTS_COMPARE[0] = 0;
        // ON time elapsed → pull pin LOW (end of pulse)
        NRF_GPIO->OUTCLR = (1UL << g_ADigitalPinMap[PIN_MOTOR]);
    }
    if (NRF_TIMER1->EVENTS_COMPARE[1]) {
        NRF_TIMER1->EVENTS_COMPARE[1] = 0;
        // Period elapsed → pull pin HIGH (start of next pulse), reload timer
        NRF_TIMER1->TASKS_CLEAR = 1;
        NRF_GPIO->OUTSET = (1UL << g_ADigitalPinMap[PIN_MOTOR]);
    }
}

// ── Internal helpers ───────────────────────────────────────────────────────
static void timerStop() {
    NRF_TIMER1->TASKS_STOP  = 1;
    NRF_TIMER1->TASKS_CLEAR = 1;
    NVIC_DisableIRQ(TIMER1_IRQn);
    NRF_GPIO->OUTCLR = (1UL << g_ADigitalPinMap[PIN_MOTOR]);
}

static void timerStart(uint8_t duty) {
    uint32_t onUs = ((uint32_t)duty * PWM_PERIOD_US) / 255u;

    NRF_TIMER1->TASKS_STOP  = 1;
    NRF_TIMER1->TASKS_CLEAR = 1;

    // 1 MHz → prescaler = 4  (16 MHz / 2^4 = 1 MHz)
    NRF_TIMER1->PRESCALER  = 4;
    NRF_TIMER1->BITMODE    = TIMER_BITMODE_BITMODE_32Bit;
    NRF_TIMER1->MODE       = TIMER_MODE_MODE_Timer;
    NRF_TIMER1->SHORTS     = 0;  // no auto-clear; we clear in CC[1] handler

    NRF_TIMER1->CC[0] = onUs;           // rising → falling edge
    NRF_TIMER1->CC[1] = PWM_PERIOD_US;  // falling → rising edge (period end)

    NRF_TIMER1->EVENTS_COMPARE[0] = 0;
    NRF_TIMER1->EVENTS_COMPARE[1] = 0;

    NRF_TIMER1->INTENSET =
        (TIMER_INTENSET_COMPARE0_Enabled << TIMER_INTENSET_COMPARE0_Pos) |
        (TIMER_INTENSET_COMPARE1_Enabled << TIMER_INTENSET_COMPARE1_Pos);

    NVIC_SetPriority(TIMER1_IRQn, 3);
    NVIC_ClearPendingIRQ(TIMER1_IRQn);
    NVIC_EnableIRQ(TIMER1_IRQn);

    // Pull pin HIGH to start first pulse, then let timer pull it LOW at CC[0]
    NRF_GPIO->OUTSET = (1UL << g_ADigitalPinMap[PIN_MOTOR]);
    NRF_TIMER1->TASKS_START = 1;
}

// ── Public API ─────────────────────────────────────────────────────────────
void motorSetup() {
    pinMode(PIN_MOTOR, OUTPUT);
    digitalWrite(PIN_MOTOR, LOW);
    g_duty = 0;
}

void motorSetDuty(uint8_t duty) {
    if (duty == g_duty) return;
    g_duty = duty;

    if (duty == 0) {
        timerStop();
    } else if (duty == 255) {
        timerStop();  // stop timer first, then hold pin HIGH
        NRF_GPIO->OUTSET = (1UL << g_ADigitalPinMap[PIN_MOTOR]);
    } else {
        timerStart(duty);
    }
}

// motorUpdate() is kept for API compatibility but no longer needed —
// the hardware timer fires the ISR independently of the main loop.
void motorUpdate() {}
