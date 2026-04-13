#include "bluetooth.h"

extern RTTStream rtt;

static bool connected = false;

void bluetoothSetup() {
    rtt.println("Bluetooth module ready");
    // TODO: init BLE stack (NimBLE / Bluefruit), set device name, configure services
}

void bluetoothLoop() {
    // TODO: service BLE events, handle RX characteristic writes
}

void bluetoothStartAdvertising() {
    rtt.println("BLE: advertising");
    // TODO: start advertising
}

void bluetoothStopAdvertising() {
    rtt.println("BLE: stop advertising");
    // TODO: stop advertising
}

bool bluetoothIsConnected() {
    return connected;
}
