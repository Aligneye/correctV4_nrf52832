#include "calibration.h"

extern RTTStream rtt;

static bool active = false;

void calibrationSetup() {
    rtt.println("Calibration module ready");
}

void calibrationLoop() {
    if (!active) return;
    // TODO: run calibration routine (sample accelerometer, compute offsets)
}

void calibrationStart() {
    if (active) return;
    active = true;
    rtt.println("Calibration: start");
    // TODO: reset accumulators, begin sampling
}

void calibrationStop() {
    if (!active) return;
    active = false;
    rtt.println("Calibration: stop");
    // TODO: commit offsets to storage
}

bool calibrationIsActive() {
    return active;
}
