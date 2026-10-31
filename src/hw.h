#pragma once
#include <Arduino.h>
#include "settings.h"

// Guards every SD access. The audio task holds it while decoding; the UI takes
// it for short reads (cover art). The library index lives in RAM, so browsing
// never touches the card.
extern SemaphoreHandle_t g_sdMutex;

void  hwBegin();                  // mutex, DAC held muted, ADC setup
bool  hwSdBegin();                // mount the SD card on VSPI
void  dacMute(bool mute);         // PCM5102 XSMT (no-op if PIN_DAC_XSMT = -1)
float batteryVolts();             // 0 if no sensor
int   batteryPercent();           // -1 if no sensor
void  switchMode(Mode m);         // persist + reboot into the other mode
