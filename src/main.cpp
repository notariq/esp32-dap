#include <Arduino.h>
#include "config.h"
#include "settings.h"
#include "ui_dap.h"
#include "mode_bt.h"

// Two firmware modes, selected at boot (stored in NVS, changed via reboot):
//   DAP : SD card library player        (Bluetooth RAM released)
//   BT  : Bluetooth A2DP receiver       (no SD / library)
// Hold BACK while powering on to force DAP mode (escape hatch).

static Mode sMode = Mode::DAP;

void setup() {
  Serial.begin(115200);
  settingsBegin();
  sMode = gCfg.mode;

  pinMode(PIN_BTN_BACK, INPUT);            // needs the external pull-up
  delay(5);
  if (digitalRead(PIN_BTN_BACK) == LOW && sMode != Mode::DAP) {
    sMode = Mode::DAP;
    gCfg.mode = Mode::DAP;
    settingsSave();
  }

  Serial.printf("\nESP32 DAP %s, mode: %s\n", FW_VERSION, sMode == Mode::BT ? "Bluetooth" : "DAP");
  if (sMode == Mode::BT) btSetup(); else dapSetup();
}

void loop() {
  if (sMode == Mode::BT) btLoop(); else dapLoop();
}
