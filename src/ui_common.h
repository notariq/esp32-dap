#pragma once
#include "display_cfg.h"

constexpr uint16_t C_BG  = 0x0000;
constexpr uint16_t C_FG  = 0xFFFF;
constexpr uint16_t C_DIM = 0x8410;
constexpr uint16_t C_ACC = 0xFC60;   // orange
constexpr uint16_t C_SEL = 0x2104;   // selected row
constexpr uint16_t C_BAR = 0x4208;   // progress track

constexpr int UI_HDR_H = 24;
constexpr int UI_TIMEOUT_COUNT = 4;

#define UI_F12 (&lgfx::fonts::lgfxJapanGothic_12)
#define UI_F16 (&lgfx::fonts::lgfxJapanGothic_16)
#define UI_F20 (&lgfx::fonts::lgfxJapanGothic_20)
#define UI_F40 (&lgfx::fonts::lgfxJapanGothic_40)

extern LGFX_Sprite gCv;                 // full-screen back buffer (PSRAM)

bool  uiInit();                         // init panel + back buffer
void  uiPush();                         // send the back buffer to the panel
void  uiFit(const char* in, int maxW, char* out, size_t n);        // ellipsize (UTF-8 safe)
bool  uiMarquee(const char* s, int x, int y, int w, uint16_t col); // true if it is scrolling
void  uiMarqueeReset();
void  uiHeader(const char* title, int battPct, int playIcon);      // icon: 0 none, 1 play, 2 pause
void  uiFmtTime(uint32_t sec, char* out);                          // m:ss
void  uiCenterText(const char* s, int y, uint16_t col);

// Screen power: backlight timeout + wake
bool        screenPoke();               // register activity; true if screen was off
void        screenTick();
bool        screenIsOff();
void        screenApplyBrightness();
const char* uiTimeoutLabel(uint8_t idx);
