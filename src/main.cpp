#include <Arduino.h>
#include <RTTStream.h>
#include "config.h"
#include "storage.h"
#include "button.h"
#include "therapy.h"
#include "training.h"
#include "calibration.h"
#include "bluetooth.h"

RTTStream rtt;

void setup() {
    rtt.println("=== AlignEye Firmware V4 ===");
    storageSetup();
    buttonSetup();
    therapySetup();
    trainingSetup();
    calibrationSetup();
    bluetoothSetup();
}

void loop() {
    buttonLoop();
    therapyLoop();
    trainingLoop();
    calibrationLoop();
    bluetoothLoop();
}
