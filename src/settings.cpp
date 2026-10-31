#include "settings.h"
#include <Preferences.h>

SettingsData gCfg;
static Preferences sPref;
static bool     sPending = false;
static uint32_t sPendingAt = 0;

void settingsBegin() {
  sPref.begin("dap", false);
  gCfg.mode          = (Mode)sPref.getUChar("mode", 0);
  gCfg.volume        = sPref.getUChar("vol", VOLUME_DEFAULT);
  gCfg.btVolume      = sPref.getUChar("btvol", 64);
  gCfg.brightness    = sPref.getUChar("bri", 70);
  gCfg.screenTimeout = sPref.getUChar("scr", 1);
  gCfg.shuffle       = sPref.getBool("shuf", false);
  gCfg.repeat        = (Repeat)sPref.getUChar("rep", 0);
  gCfg.lastBase      = sPref.getUInt("lb", 0);
  gCfg.lastCount     = sPref.getUInt("lc", 0);
  gCfg.lastOffset    = sPref.getUInt("lo", 0);

  // sanitize
  if ((uint8_t)gCfg.mode > 1) gCfg.mode = Mode::DAP;
  if (gCfg.volume > VOLUME_MAX) gCfg.volume = VOLUME_MAX;
  if (gCfg.btVolume > 127) gCfg.btVolume = 127;
  if (gCfg.brightness < 5 || gCfg.brightness > 100) gCfg.brightness = 70;
  if (gCfg.screenTimeout > 3) gCfg.screenTimeout = 1;
  if ((uint8_t)gCfg.repeat > 2) gCfg.repeat = Repeat::Off;
}

void settingsSave() {
  sPref.putUChar("mode", (uint8_t)gCfg.mode);
  sPref.putUChar("vol", gCfg.volume);
  sPref.putUChar("btvol", gCfg.btVolume);
  sPref.putUChar("bri", gCfg.brightness);
  sPref.putUChar("scr", gCfg.screenTimeout);
  sPref.putBool("shuf", gCfg.shuffle);
  sPref.putUChar("rep", (uint8_t)gCfg.repeat);
  sPref.putUInt("lb", gCfg.lastBase);
  sPref.putUInt("lc", gCfg.lastCount);
  sPref.putUInt("lo", gCfg.lastOffset);
  sPending = false;
}

void settingsDirty() { sPending = true; sPendingAt = millis(); }

void settingsTick() {
  if (sPending && millis() - sPendingAt > 2000) settingsSave();
}
