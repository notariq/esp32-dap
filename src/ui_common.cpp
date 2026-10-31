#include "ui_common.h"
#include "settings.h"

LGFX_Sprite gCv(&gLcd);

static uint32_t sEpoch   = 0;
static uint32_t sLastAct = 0;
static bool     sOff     = false;

static const uint32_t kTimeoutMs[UI_TIMEOUT_COUNT]  = {15000, 30000, 60000, 0};
static const char* const kTimeoutLbl[UI_TIMEOUT_COUNT] = {"15 s", "30 s", "60 s", "Never"};
const char* uiTimeoutLabel(uint8_t i) { return kTimeoutLbl[i % UI_TIMEOUT_COUNT]; }

bool uiInit() {
  gLcd.init();
  gLcd.setRotation(TFT_ROTATION);
  gLcd.setBrightness(0);
  gLcd.fillScreen(C_BG);
  gCv.setColorDepth(16);
  gCv.setPsram(true);
  bool ok = gCv.createSprite(gLcd.width(), gLcd.height()) != nullptr;
  gCv.setFont(UI_F16);
  screenApplyBrightness();
  sLastAct = millis();
  return ok;
}

void uiPush() { gCv.pushSprite(0, 0); }

void uiFit(const char* in, int maxW, char* out, size_t n) {
  strlcpy(out, in, n);
  if (gCv.textWidth(out) <= maxW) return;
  size_t len = strlen(in);
  if (len > n - 4) len = n - 4;
  while (len > 0) {
    do { len--; } while (len > 0 && ((uint8_t)in[len] & 0xC0) == 0x80);  // drop whole UTF-8 chars
    memcpy(out, in, len);
    strcpy(out + len, "...");
    if (gCv.textWidth(out) <= maxW) return;
  }
  strcpy(out, "...");
}

void uiMarqueeReset() { sEpoch = millis(); }

bool uiMarquee(const char* s, int x, int y, int w, uint16_t col) {
  int tw = gCv.textWidth(s);
  gCv.setTextColor(col);
  gCv.setClipRect(x, y, w, gCv.fontHeight());
  bool scrolling = tw > w;
  if (!scrolling) {
    gCv.drawString(s, x, y);
  } else {
    const int gap = 40;
    int span = tw + gap;
    uint32_t el = millis() - sEpoch;
    int off = el < 1500 ? 0 : (int)(((el - 1500) / 25) % span);   // ~40 px/s, 1.5 s pause
    gCv.drawString(s, x - off, y);
    gCv.drawString(s, x - off + span, y);
  }
  gCv.clearClipRect();
  return scrolling;
}

void uiHeader(const char* title, int battPct, int playIcon) {
  const int W = gCv.width();
  gCv.fillRect(0, 0, W, UI_HDR_H, C_SEL);
  gCv.setFont(UI_F16);
  gCv.setTextColor(C_FG);
  int right = W - 6;
  if (battPct >= 0) {
    char b[8];
    snprintf(b, sizeof b, "%d%%", battPct);
    gCv.setFont(UI_F12);
    int bw = gCv.textWidth(b);
    gCv.setTextColor(battPct < 15 ? (uint16_t)0xF800 : C_DIM);
    gCv.drawString(b, right - bw, 6);
    right -= bw + 8;
  }
  if (playIcon == 1) {
    gCv.fillTriangle(right - 9, 7, right - 9, 17, right, 12, C_ACC);
    right -= 16;
  } else if (playIcon == 2) {
    gCv.fillRect(right - 9, 7, 3, 10, C_ACC);
    gCv.fillRect(right - 4, 7, 3, 10, C_ACC);
    right -= 16;
  }
  gCv.setFont(UI_F16);
  char t[96];
  uiFit(title, right - 12, t, sizeof t);
  gCv.setTextColor(C_FG);
  gCv.drawString(t, 6, 4);
}

void uiFmtTime(uint32_t sec, char* out) {
  snprintf(out, 12, "%u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
}

void uiCenterText(const char* s, int y, uint16_t col) {
  gCv.setTextColor(col);
  gCv.setTextDatum(lgfx::textdatum_t::top_center);
  gCv.drawString(s, gCv.width() / 2, y);
  gCv.setTextDatum(lgfx::textdatum_t::top_left);
}

// ---- screen power -----------------------------------------------------------
void screenApplyBrightness() { gLcd.setBrightness((uint32_t)gCfg.brightness * 255 / 100); }

bool screenPoke() {
  sLastAct = millis();
  if (sOff) { sOff = false; screenApplyBrightness(); return true; }
  return false;
}

void screenTick() {
  uint32_t to = kTimeoutMs[gCfg.screenTimeout % UI_TIMEOUT_COUNT];
  if (!sOff && to && millis() - sLastAct > to) { sOff = true; gLcd.setBrightness(0); }
}

bool screenIsOff() { return sOff; }
