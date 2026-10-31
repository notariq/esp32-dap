#include "hw.h"
#include "display_cfg.h"
#include <SD.h>
#include <SPI.h>

LGFX gLcd;
SemaphoreHandle_t g_sdMutex = nullptr;

void hwBegin() {
  g_sdMutex = xSemaphoreCreateMutex();
#if PIN_DAC_XSMT >= 0
  pinMode(PIN_DAC_XSMT, OUTPUT);
  digitalWrite(PIN_DAC_XSMT, LOW);          // muted until firmware says otherwise
#endif
#if HAS_BATTERY_SENSE
  analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db);
#endif
}

bool hwSdBegin() {
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  return SD.begin(PIN_SD_CS, SPI, SD_FREQ_HZ);
}

void dacMute(bool mute) {
#if PIN_DAC_XSMT >= 0
  digitalWrite(PIN_DAC_XSMT, mute ? LOW : HIGH);
#else
  (void)mute;
#endif
}

float batteryVolts() {
#if HAS_BATTERY_SENSE
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(PIN_BATT_ADC);
  return (mv / 16.0f) * BATT_DIV_RATIO / 1000.0f;
#else
  return 0;
#endif
}

int batteryPercent() {
#if HAS_BATTERY_SENSE
  // Rough Li-ion discharge curve (resting voltage). Good enough for a gauge.
  static const float V[]   = {4.20f, 4.10f, 4.00f, 3.90f, 3.80f, 3.70f, 3.60f, 3.50f, 3.30f};
  static const int   PCT[] = { 100,   92,    82,    68,    50,    32,    15,     6,     0 };
  float v = batteryVolts();
  if (v >= V[0]) return 100;
  for (int i = 1; i < 9; i++) {
    if (v >= V[i]) {
      float t = (v - V[i]) / (V[i - 1] - V[i]);
      return (int)(PCT[i] + t * (PCT[i - 1] - PCT[i]));
    }
  }
  return 0;
#else
  return -1;
#endif
}

void switchMode(Mode m) {
  dacMute(true);
  gCfg.mode = m;
  settingsSave();
  delay(150);
  ESP.restart();
}
