#include "training.h"

extern RTTStream rtt;

void trainingSetup() {
    rtt.println("Training module ready");
}

void trainingLoop() {
    // TODO: training session state machine
}

void trainingStart() {
    rtt.println("Training: start");
    // TODO: begin selected training sub-mode (Instant / Delayed)
}

void trainingStop() {
    rtt.println("Training: stop");
    // TODO: end training session
}
