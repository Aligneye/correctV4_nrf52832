#pragma once

#include <Arduino.h>
#include <RTTStream.h>
#include "config.h"

void bluetoothSetup();
void bluetoothLoop();

void bluetoothStartAdvertising();
void bluetoothStopAdvertising();
bool bluetoothIsConnected();
