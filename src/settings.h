#pragma once
#include <Arduino.h>
#include "config.h"

enum class Mode : uint8_t   { DAP = 0, BT = 1 };
enum class Repeat : uint8_t { Off = 0, All = 1, One = 2 };

struct SettingsData {
  Mode     mode          = Mode::DAP;
  uint8_t  volume        = VOLUME_DEFAULT;  // DAP volume 0..VOLUME_MAX
  uint8_t  btVolume      = 64;              // Bluetooth volume 0..127
  uint8_t  brightness    = 70;              // percent
  uint8_t  screenTimeout = 1;               // index, see ui_common.cpp
  bool     shuffle       = false;
  Repeat   repeat        = Repeat::Off;
  uint32_t lastBase = 0, lastCount = 0, lastOffset = 0;   // "resume last" info
};

extern SettingsData gCfg;

void settingsBegin();   // load from NVS
void settingsSave();    // write to NVS now
void settingsDirty();   // schedule a write (flushed by settingsTick after ~2 s)
void settingsTick();    // call from the UI loop
