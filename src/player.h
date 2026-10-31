#pragma once
#include <Arduino.h>
#include "settings.h"

enum class PState : uint8_t { Stopped, Playing, Paused };

// Written by the audio task, read by the UI task (32-bit fields: atomic on ESP32).
struct PlayerStatus {
  volatile PState   state       = PState::Stopped;
  volatile uint32_t track       = 0;      // library track index of the current song
  volatile uint32_t trackSerial = 0;      // bumps on every track change (0 = never played)
  volatile uint32_t posSec      = 0;
  volatile uint32_t durSec      = 0;
  volatile uint32_t sampleRate  = 0;
  volatile uint8_t  bits        = 0;
  volatile uint32_t qPos        = 0;      // position in the (possibly shuffled) queue
  volatile uint32_t qLen        = 0;
};

enum class PCmd : uint8_t {
  PlayRange,    // a = first track, b = count, c = start offset inside the range
  TogglePause,
  Next,
  Prev,
  SetVolume,    // a = 0..VOLUME_MAX
  SetShuffle,   // a = 0/1
  SetRepeat,    // a = Repeat
  Stop
};

class Player {
 public:
  void begin();   // start the audio task (call after SD + library are ready)
  void send(PCmd c, uint32_t a = 0, uint32_t b = 0, uint32_t cc = 0);
  const PlayerStatus& status() const;
};

extern Player g_player;
