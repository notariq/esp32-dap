#include "mode_bt.h"
#include "config.h"
#include "hw.h"
#include "input.h"
#include "settings.h"
#include "ui_common.h"
#include <BluetoothA2DPSink.h>

// ============================================================================
//  Bluetooth receiver mode: phone -> A2DP sink -> I2S -> PCM5102
// ============================================================================

static BluetoothA2DPSink sA2dp;
static char sTitle[96], sArtist[64], sAlbum[64];
static volatile bool sMetaChanged = false;
static bool     sWasConn = false;
static bool     sDirty = true, sAnim = false;
static int      sBatt = -1;
static uint32_t sBattAt = 0, sLastDraw = 0;

// called from the Bluetooth task: just copy strings and raise a flag
static void onMeta(uint8_t id, const uint8_t* text) {
  const char* t = (const char*)text;
  switch (id) {
    case ESP_AVRC_MD_ATTR_TITLE:  strlcpy(sTitle,  t, sizeof sTitle);  break;
    case ESP_AVRC_MD_ATTR_ARTIST: strlcpy(sArtist, t, sizeof sArtist); break;
    case ESP_AVRC_MD_ATTR_ALBUM:  strlcpy(sAlbum,  t, sizeof sAlbum);  break;
    default: return;
  }
  sMetaChanged = true;
}

static bool isPlaying() {
  return sA2dp.is_connected() && sA2dp.get_audio_state() == ESP_A2D_AUDIO_STATE_STARTED;
}

static void draw() {
  const int W = gCv.width(), H = gCv.height();
  bool conn = sA2dp.is_connected();
  bool playing = isPlaying();
  gCv.fillScreen(C_BG);
  uiHeader("Bluetooth", sBatt, conn ? (playing ? 1 : 2) : 0);
  sAnim = false;

  if (!conn) {
    gCv.setFont(UI_F20);
    uiCenterText("Waiting for phone", H / 2 - 50, C_FG);
    gCv.setFont(UI_F16);
    uiCenterText("Pair with:", H / 2 - 12, C_DIM);
    uiCenterText(BT_DEVICE_NAME, H / 2 + 12, C_ACC);
  } else {
    int y = UI_HDR_H + 36;
    gCv.setFont(UI_F12);
    uiCenterText("Connected", y, C_DIM);
    y += 44;
    gCv.setFont(UI_F20);
    sAnim |= uiMarquee(sTitle[0] ? sTitle : "(no track info)", 10, y, W - 20, C_FG);
    y += 30;
    gCv.setFont(UI_F16);
    sAnim |= uiMarquee(sArtist, 10, y, W - 20, C_ACC);
    y += 24;
    sAnim |= uiMarquee(sAlbum, 10, y, W - 20, C_DIM);
  }

  // volume bar
  int by = H - 46;
  gCv.fillRoundRect(10, by, W - 20, 4, 2, C_BAR);
  gCv.fillRoundRect(10, by, (W - 20) * gCfg.btVolume / 127, 4, 2, C_ACC);
  gCv.setFont(UI_F12);
  uiCenterText("Hold encoder: back to DAP mode", H - 20, C_DIM);
  uiPush();
  sDirty = false;
  sLastDraw = millis();
}

static void btVol(int d) {
  int v = constrain((int)gCfg.btVolume + d * 4, 0, 127);
  gCfg.btVolume = v;
  sA2dp.set_volume(v);
  settingsDirty();
}

static void togglePlay() { if (isPlaying()) sA2dp.pause(); else sA2dp.play(); }

static void handleEvent(Ev e) {
  bool wasOff = screenPoke();
  sDirty = true;
  uiMarqueeReset();
  switch (e) {
    case Ev::CW:       btVol(+1); break;
    case Ev::CCW:      btVol(-1); break;
    case Ev::Play:     togglePlay(); break;
    case Ev::PlayLong: sA2dp.next(); break;
    case Ev::BackLong: sA2dp.previous(); break;
    case Ev::Click:    if (!wasOff) togglePlay(); break;
    case Ev::LongClick: switchMode(Mode::DAP); break;
    default: break;
  }
}

void btSetup() {
  setCpuFrequencyMhz(CPU_MHZ);
  hwBegin();
  inputBegin();
  if (!uiInit()) Serial.println("[ui] back buffer allocation failed (is PSRAM enabled?)");

  i2s_pin_config_t pins = {
    .mck_io_num   = I2S_PIN_NO_CHANGE,
    .bck_io_num   = PIN_I2S_BCK,
    .ws_io_num    = PIN_I2S_LRC,
    .data_out_num = PIN_I2S_DIN,
    .data_in_num  = I2S_PIN_NO_CHANGE,
  };
  sA2dp.set_pin_config(pins);
  sA2dp.set_auto_reconnect(true);
  sA2dp.set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST |
                                         ESP_AVRC_MD_ATTR_ALBUM);
  sA2dp.set_avrc_metadata_callback(onMeta);
  sA2dp.start(BT_DEVICE_NAME);
  sA2dp.set_volume(gCfg.btVolume);

  delay(300);
  dacMute(false);                      // I2S is running (silence) -> safe to unmute
}

void btLoop() {
  uint32_t now = millis();
  for (Ev e; (e = inputPoll()) != Ev::None;) handleEvent(e);

  bool conn = sA2dp.is_connected();
  if (conn != sWasConn) {
    sWasConn = conn;
    if (!conn) { sTitle[0] = sArtist[0] = sAlbum[0] = 0; }
    uiMarqueeReset();
    sDirty = true;
  }
  if (sMetaChanged) { sMetaChanged = false; uiMarqueeReset(); sDirty = true; }
  if (sBattAt == 0 || now - sBattAt > 15000) { sBatt = batteryPercent(); sBattAt = now; sDirty = true; }

  screenTick();
  settingsTick();

  if (!screenIsOff() && (sDirty || (sAnim && now - sLastDraw > 40) || now - sLastDraw > 1000)) draw();
  delay(5);
}
