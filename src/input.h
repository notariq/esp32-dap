#pragma once
#include <Arduino.h>

enum class Ev : uint8_t {
  None, CW, CCW, Click, LongClick, Back, BackLong, Play, PlayLong
};

void inputBegin();
Ev   inputPoll();   // returns one event per call, Ev::None when the queue is empty
