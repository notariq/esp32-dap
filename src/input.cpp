#include "input.h"
#include "config.h"
#include <ESP32Encoder.h>

static ESP32Encoder sEnc;
static int64_t      sLastCount = 0;

struct Btn { uint8_t pin; bool down; bool longFired; uint32_t tDown; uint32_t tChange; };
static Btn sBtn[3] = {
  {PIN_ENC_SW,   false, false, 0, 0},
  {PIN_BTN_BACK, false, false, 0, 0},
  {PIN_BTN_PLAY, false, false, 0, 0},
};
static const Ev kShort[3] = {Ev::Click,     Ev::Back,     Ev::Play};
static const Ev kLong[3]  = {Ev::LongClick, Ev::BackLong, Ev::PlayLong};

static Ev      sFifo[8];
static uint8_t sHead = 0, sTail = 0;
static void push(Ev e) {
  uint8_t n = (sHead + 1) & 7;
  if (n != sTail) { sFifo[sHead] = e; sHead = n; }
}

// returns 0 = nothing, 1 = short press (on release), 2 = long press (while held)
static int btnUpdate(Btn& b, uint32_t now) {
  bool raw = digitalRead(b.pin) == LOW;
  if (raw != b.down && now - b.tChange > 20) {
    b.tChange = now;
    b.down = raw;
    if (raw) { b.tDown = now; b.longFired = false; }
    else if (!b.longFired) return 1;
  }
  if (b.down && !b.longFired && now - b.tDown > LONG_PRESS_MS) {
    b.longFired = true;
    return 2;
  }
  return 0;
}

void inputBegin() {
  for (auto& b : sBtn) {
    bool inputOnly = (b.pin >= 34 && b.pin <= 39);    // no internal pull-up there
    pinMode(b.pin, inputOnly ? INPUT : INPUT_PULLUP);
  }
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  sEnc.attachFullQuad(PIN_ENC_A, PIN_ENC_B);
  sEnc.clearCount();
  sLastCount = 0;
}

Ev inputPoll() {
  uint32_t now = millis();

  int64_t c = sEnc.getCount();
  int d = (int)((c - sLastCount) / ENC_COUNTS_PER_DETENT);
  if (d != 0) {
    sLastCount += (int64_t)d * ENC_COUNTS_PER_DETENT;
    bool cw = d > 0;
    if (ENC_INVERT) cw = !cw;
    int n = abs(d);
    if (n > 4) n = 4;
    for (int i = 0; i < n; i++) push(cw ? Ev::CW : Ev::CCW);
  }

  for (int i = 0; i < 3; i++) {
    int r = btnUpdate(sBtn[i], now);
    if (r == 1) push(kShort[i]);
    else if (r == 2) push(kLong[i]);
  }

  if (sTail == sHead) return Ev::None;
  Ev e = sFifo[sTail];
  sTail = (sTail + 1) & 7;
  return e;
}
