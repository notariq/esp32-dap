#include "ui_dap.h"
#include "config.h"
#include "hw.h"
#include "input.h"
#include "library.h"
#include "player.h"
#include "settings.h"
#include "ui_common.h"
#include <SD.h>
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>

// ============================================================================
//  DAP mode: library browser + Now Playing
// ============================================================================

enum class LK : uint8_t { Home, Artists, ArtistAlbums, Albums, AlbumTracks, Songs, Settings, About };
enum : uint8_t { H_RESUME, H_NOW, H_ARTISTS, H_ALBUMS, H_SONGS, H_BT, H_SETTINGS };
enum : uint8_t { S_SHUFFLE, S_REPEAT, S_BRIGHT, S_SCREEN, S_SLEEP, S_ABOUT, S_COUNT };

struct Nav { LK kind; uint32_t param; int cursor; int top; };

static Nav     sNav[8];
static int     sSp = 0;
static bool    sNowView = false;
static bool    sDirty = true, sAnim = false;
static uint32_t sLastDraw = 0, sLastAct = 0;
static uint8_t sHome[8];
static int     sHomeN = 0;

static int      sBatt = -1;
static uint32_t sBattAt = 0;

static uint8_t  sSleepIdx = 0;
static uint32_t sSleepAt = 0;
static const uint8_t kSleepMin[] = {0, 15, 30, 60, 90};

// change detection
static uint32_t sSerial = 0, sPos = 0xFFFFFFFF;
static PState   sState = PState::Stopped;
static uint8_t  sVol = 255;

// cover art cache (current album only)
static uint8_t* sCover = nullptr;
static size_t   sCoverLen = 0;
static uint32_t sCoverAlbum = 0xFFFFFFFF;

// ---- helpers ----------------------------------------------------------------
static const char* repeatName(Repeat r) { return r == Repeat::One ? "One" : r == Repeat::All ? "All" : "Off"; }

static void buildHome() {
  sHomeN = 0;
  const PlayerStatus& st = g_player.status();
  if (st.trackSerial == 0 && gCfg.lastCount > 0) sHome[sHomeN++] = H_RESUME;
  if (st.state != PState::Stopped || st.trackSerial > 0) sHome[sHomeN++] = H_NOW;
  sHome[sHomeN++] = H_ARTISTS;
  sHome[sHomeN++] = H_ALBUMS;
  sHome[sHomeN++] = H_SONGS;
  sHome[sHomeN++] = H_BT;
  sHome[sHomeN++] = H_SETTINGS;
}

static uint32_t listCount(const Nav& n) {
  switch (n.kind) {
    case LK::Home:         buildHome(); return sHomeN;
    case LK::Artists:      return g_lib.nArtists;
    case LK::ArtistAlbums: return g_lib.artist(n.param).albumCount;
    case LK::Albums:       return g_lib.nAlbums;
    case LK::AlbumTracks:  return g_lib.album(n.param).trackCount;
    case LK::Songs:        return g_lib.nTracks;
    case LK::Settings:     return S_COUNT;
    case LK::About:        return 6;
  }
  return 0;
}

static bool twoLine(LK k) {
  return k == LK::Artists || k == LK::ArtistAlbums || k == LK::Albums || k == LK::Songs;
}

static void navTitle(const Nav& n, char* out, size_t sz) {
  switch (n.kind) {
    case LK::Home:         strlcpy(out, "ESP32 DAP", sz); break;
    case LK::Artists:      strlcpy(out, "Artists", sz); break;
    case LK::ArtistAlbums: strlcpy(out, g_lib.artist(n.param).name, sz); break;
    case LK::Albums:       strlcpy(out, "Albums", sz); break;
    case LK::AlbumTracks:  strlcpy(out, g_lib.album(n.param).name, sz); break;
    case LK::Songs:        strlcpy(out, "Songs", sz); break;
    case LK::Settings:     strlcpy(out, "Settings", sz); break;
    case LK::About:        strlcpy(out, "About", sz); break;
  }
}

// a = main text, b = secondary text (second line, or right-aligned value)
static void rowText(const Nav& n, uint32_t i, char* a, size_t na, char* b, size_t nb) {
  a[0] = b[0] = 0;
  switch (n.kind) {
    case LK::Home: {
      static const char* const names[] = {"Resume last", "Now Playing", "Artists", "Albums",
                                          "Songs", "Bluetooth Receiver", "Settings"};
      snprintf(a, na, "%s", names[sHome[i]]);
      break;
    }
    case LK::Artists: {
      const ArtistRec& r = g_lib.artist(i);
      snprintf(a, na, "%s", r.name);
      snprintf(b, nb, "%u albums, %u songs", (unsigned)r.albumCount, (unsigned)r.trackCount);
      break;
    }
    case LK::ArtistAlbums: {
      const AlbumRec& al = g_lib.album(g_lib.artist(n.param).firstAlbum + i);
      snprintf(a, na, "%s", al.name);
      if (al.year) snprintf(b, nb, "%u  |  %u songs", (unsigned)al.year, (unsigned)al.trackCount);
      else         snprintf(b, nb, "%u songs", (unsigned)al.trackCount);
      break;
    }
    case LK::Albums: {
      const AlbumRec& al = g_lib.album(i);
      snprintf(a, na, "%s", al.name);
      snprintf(b, nb, "%s", g_lib.artist(al.artistIdx).name);
      break;
    }
    case LK::AlbumTracks: {
      const TrackRec& t = g_lib.track(g_lib.album(n.param).firstTrack + i);
      if (t.trackNo) snprintf(a, na, "%02u  %s", (unsigned)t.trackNo, t.title);
      else           snprintf(a, na, "%s", t.title);
      uiFmtTime(t.durationSec, b);
      break;
    }
    case LK::Songs: {
      const TrackRec& t = g_lib.track(i);
      snprintf(a, na, "%s", t.title);
      snprintf(b, nb, "%s", g_lib.artist(t.artistIdx).name);
      break;
    }
    case LK::Settings:
      switch (i) {
        case S_SHUFFLE: snprintf(a, na, "Shuffle");     snprintf(b, nb, "%s", gCfg.shuffle ? "On" : "Off"); break;
        case S_REPEAT:  snprintf(a, na, "Repeat");      snprintf(b, nb, "%s", repeatName(gCfg.repeat)); break;
        case S_BRIGHT:  snprintf(a, na, "Brightness");  snprintf(b, nb, "%u%%", (unsigned)gCfg.brightness); break;
        case S_SCREEN:  snprintf(a, na, "Screen off");  snprintf(b, nb, "%s", uiTimeoutLabel(gCfg.screenTimeout)); break;
        case S_SLEEP:   snprintf(a, na, "Sleep timer");
                        if (kSleepMin[sSleepIdx]) snprintf(b, nb, "%u min", (unsigned)kSleepMin[sSleepIdx]);
                        else snprintf(b, nb, "Off");
                        break;
        case S_ABOUT:   snprintf(a, na, "About"); break;
      }
      break;
    case LK::About:
      switch (i) {
        case 0: snprintf(a, na, "Firmware");   snprintf(b, nb, "%s", FW_VERSION); break;
        case 1: snprintf(a, na, "Artists");    snprintf(b, nb, "%u", (unsigned)g_lib.nArtists); break;
        case 2: snprintf(a, na, "Albums");     snprintf(b, nb, "%u", (unsigned)g_lib.nAlbums); break;
        case 3: snprintf(a, na, "Songs");      snprintf(b, nb, "%u", (unsigned)g_lib.nTracks); break;
        case 4: snprintf(a, na, "Free heap");  snprintf(b, nb, "%u KB", (unsigned)(ESP.getFreeHeap() / 1024)); break;
        case 5: snprintf(a, na, "Free PSRAM"); snprintf(b, nb, "%u KB", (unsigned)(ESP.getFreePsram() / 1024)); break;
      }
      break;
  }
}

static int hdrIcon() {
  PState s = g_player.status().state;
  return s == PState::Playing ? 1 : s == PState::Paused ? 2 : 0;
}

// ---- cover art ----------------------------------------------------------------
static void loadCover(uint32_t albumIdx) {
  if (albumIdx == sCoverAlbum) return;
  sCoverAlbum = albumIdx;
  sCoverLen = 0;
  if (!sCover) {
    sCover = (uint8_t*)heap_caps_malloc(COVER_BUF_BYTES, MALLOC_CAP_SPIRAM);
    if (!sCover) sCover = (uint8_t*)malloc(COVER_BUF_BYTES);
    if (!sCover) return;
  }
  if (!g_lib.album(albumIdx).hasCover) return;
  char p[48];
  snprintf(p, sizeof p, INDEX_DIR "/covers/%u.jpg", (unsigned)albumIdx);
  xSemaphoreTake(g_sdMutex, portMAX_DELAY);
  File f = SD.open(p, FILE_READ);
  if (f) {
    size_t n = f.size();
    if (n > 0 && n <= COVER_BUF_BYTES) sCoverLen = f.read(sCover, n);
    f.close();
  }
  xSemaphoreGive(g_sdMutex);
}

// ---- drawing ------------------------------------------------------------------
static void drawList() {
  Nav& n = sNav[sSp];
  const int W = gCv.width(), H = gCv.height();
  gCv.fillScreen(C_BG);

  char title[112];
  navTitle(n, title, sizeof title);
  uiHeader(title, sBatt, hdrIcon());
  sAnim = false;

  uint32_t cnt = listCount(n);
  if (cnt == 0) {
    gCv.setFont(UI_F16);
    uiCenterText("Empty", H / 2 - 8, C_DIM);
    return;
  }

  const bool two = twoLine(n.kind);
  const int rowH = two ? 40 : 30;
  const int rows = (H - UI_HDR_H) / rowH;
  if (n.cursor >= (int)cnt) n.cursor = cnt - 1;
  if (n.cursor < n.top) n.top = n.cursor;
  if (n.cursor >= n.top + rows) n.top = n.cursor - rows + 1;

  char a[128], b[96], t[128];
  for (int r = 0; r < rows; r++) {
    uint32_t idx = n.top + r;
    if (idx >= cnt) break;
    int y = UI_HDR_H + r * rowH;
    bool sel = (int)idx == n.cursor;
    if (sel) {
      gCv.fillRect(0, y, W - 6, rowH, C_SEL);
      gCv.fillRect(0, y, 3, rowH, C_ACC);
    }
    rowText(n, idx, a, sizeof a, b, sizeof b);

    gCv.setFont(UI_F16);
    uint16_t fg = sel ? C_FG : (uint16_t)0xC618;
    if (two) {
      int w = W - 22;
      if (sel) sAnim |= uiMarquee(a, 10, y + 3, w, fg);
      else { uiFit(a, w, t, sizeof t); gCv.setTextColor(fg); gCv.drawString(t, 10, y + 3); }
      gCv.setFont(UI_F12);
      uiFit(b, w, t, sizeof t);
      gCv.setTextColor(C_DIM);
      gCv.drawString(t, 10, y + 23);
    } else {
      int vw = b[0] ? gCv.textWidth(b) + 12 : 0;
      int w = W - 22 - vw;
      int ty = y + (rowH - 16) / 2;
      if (sel) sAnim |= uiMarquee(a, 10, ty, w, fg);
      else { uiFit(a, w, t, sizeof t); gCv.setTextColor(fg); gCv.drawString(t, 10, ty); }
      if (b[0]) {
        gCv.setTextColor(sel ? C_ACC : C_DIM);
        gCv.drawString(b, W - 12 - (vw - 12), ty);
      }
    }
  }

  // scrollbar
  if ((int)cnt > rows) {
    int trackH = rows * rowH;
    int barH = max(10, trackH * rows / (int)cnt);
    int barY = UI_HDR_H + (trackH - barH) * n.top / ((int)cnt - rows);
    gCv.fillRect(W - 3, UI_HDR_H, 2, trackH, C_BAR);
    gCv.fillRect(W - 3, barY, 2, barH, C_ACC);
  }
}

static void drawNow() {
  const PlayerStatus& s = g_player.status();
  const int W = gCv.width(), H = gCv.height();
  gCv.fillScreen(C_BG);
  uiHeader("Now Playing", sBatt, hdrIcon());
  sAnim = false;

  if (s.trackSerial == 0) {
    gCv.setFont(UI_F16);
    uiCenterText("Nothing playing", H / 2 - 8, C_DIM);
    return;
  }

  const TrackRec&  t  = g_lib.track(s.track);
  const AlbumRec&  al = g_lib.album(t.albumIdx);
  const ArtistRec& ar = g_lib.artist(t.artistIdx);

  // cover, centred in the space above the text block
  const int cs = COVER_SIZE;
  const int cx = (W - cs) / 2;
  const int cy = UI_HDR_H + max(6, (H - UI_HDR_H - 112 - cs) / 2);
  if (sCoverLen) {
    gCv.drawJpg(sCover, sCoverLen, cx, cy);
  } else {
    gCv.fillRoundRect(cx, cy, cs, cs, 6, C_SEL);
    char ch[5] = {0};
    const char* nm = al.name[0] ? al.name : "?";
    int l = 1;
    while (l < 4 && ((uint8_t)nm[l] & 0xC0) == 0x80) l++;
    memcpy(ch, nm, l);
    gCv.setFont(UI_F40);
    gCv.setTextColor(C_DIM);
    gCv.setTextDatum(lgfx::textdatum_t::middle_center);
    gCv.drawString(ch, cx + cs / 2, cy + cs / 2);
    gCv.setTextDatum(lgfx::textdatum_t::top_left);
  }

  int y = cy + cs + 8;
  gCv.setFont(UI_F20);
  sAnim |= uiMarquee(t.title, 10, y, W - 20, C_FG);
  y += 26;
  gCv.setFont(UI_F16);
  sAnim |= uiMarquee(ar.name, 10, y, W - 20, C_ACC);
  y += 22;
  sAnim |= uiMarquee(al.name, 10, y, W - 20, C_DIM);

  // progress
  uint32_t dur = s.durSec ? s.durSec : t.durationSec;
  uint32_t pos = s.posSec;
  if (dur && pos > dur) pos = dur;
  const int barY = H - 44;
  gCv.fillRoundRect(10, barY, W - 20, 4, 2, C_BAR);
  if (dur) gCv.fillRoundRect(10, barY, (int)((int64_t)(W - 20) * pos / dur), 4, 2, C_ACC);
  char tm[12];
  gCv.setFont(UI_F12);
  gCv.setTextColor(C_DIM);
  uiFmtTime(pos, tm);
  gCv.drawString(tm, 10, H - 36);
  uiFmtTime(dur, tm);
  gCv.setTextDatum(lgfx::textdatum_t::top_right);
  gCv.drawString(tm, W - 10, H - 36);

  // info line: format + queue position on the left, mode flags + volume on the right
  const char* dot = strrchr(t.path, '.');
  char ext[8] = "";
  if (dot) { strlcpy(ext, dot + 1, sizeof ext); for (char* p = ext; *p; p++) *p = toupper(*p); }
  char info[48];
  if (s.sampleRate) snprintf(info, sizeof info, "%s %.1fk/%u  %u/%u", ext, s.sampleRate / 1000.0f,
                             (unsigned)s.bits, (unsigned)s.qPos + 1, (unsigned)s.qLen);
  else              snprintf(info, sizeof info, "%s  %u/%u", ext, (unsigned)s.qPos + 1, (unsigned)s.qLen);
  gCv.setTextDatum(lgfx::textdatum_t::top_left);
  gCv.drawString(info, 10, H - 18);

  char flags[32];
  snprintf(flags, sizeof flags, "%s%s Vol %u", gCfg.shuffle ? "S " : "",
           gCfg.repeat == Repeat::One ? "R1" : gCfg.repeat == Repeat::All ? "RA" : "",
           (unsigned)gCfg.volume);
  gCv.setTextDatum(lgfx::textdatum_t::top_right);
  gCv.drawString(flags, W - 10, H - 18);
  gCv.setTextDatum(lgfx::textdatum_t::top_left);
}

static void render() {
  if (sNowView) drawNow(); else drawList();
  uiPush();
  sDirty = false;
  sLastDraw = millis();
}

// ---- actions ----------------------------------------------------------------------
static void push(LK k, uint32_t p) {
  if (sSp < 7) { sSp++; sNav[sSp] = {k, p, 0, 0}; }
}

static void volStep(int d) {
  int v = constrain((int)gCfg.volume + d, 0, VOLUME_MAX);
  gCfg.volume = v;                       // update now so rapid turns accumulate
  g_player.send(PCmd::SetVolume, v);
  settingsDirty();
}

static void resumeLast() {
  if (gCfg.lastCount == 0 || gCfg.lastBase + gCfg.lastCount > g_lib.nTracks) return;
  g_player.send(PCmd::PlayRange, gCfg.lastBase, gCfg.lastCount, gCfg.lastOffset);
  sNowView = true;
}

static void settingAction(int i) {
  switch (i) {
    case S_SHUFFLE:
      gCfg.shuffle = !gCfg.shuffle;
      g_player.send(PCmd::SetShuffle, gCfg.shuffle);
      break;
    case S_REPEAT:
      gCfg.repeat = (Repeat)(((uint8_t)gCfg.repeat + 1) % 3);
      g_player.send(PCmd::SetRepeat, (uint32_t)gCfg.repeat);
      break;
    case S_BRIGHT:
      gCfg.brightness = gCfg.brightness >= 100 ? 20 : gCfg.brightness + 20;
      screenApplyBrightness();
      settingsDirty();
      break;
    case S_SCREEN:
      gCfg.screenTimeout = (gCfg.screenTimeout + 1) % UI_TIMEOUT_COUNT;
      settingsDirty();
      break;
    case S_SLEEP:
      sSleepIdx = (sSleepIdx + 1) % (sizeof kSleepMin);
      sSleepAt = kSleepMin[sSleepIdx] ? millis() + (uint32_t)kSleepMin[sSleepIdx] * 60000UL : 0;
      break;
    case S_ABOUT:
      push(LK::About, 0);
      break;
  }
}

static void selectItem() {
  Nav& n = sNav[sSp];
  uint32_t cnt = listCount(n);
  if (cnt == 0 || n.cursor >= (int)cnt) return;
  uint32_t c = n.cursor, param = n.param;
  switch (n.kind) {
    case LK::Home:
      switch (sHome[c]) {
        case H_RESUME:   resumeLast(); break;
        case H_NOW:      sNowView = true; break;
        case H_ARTISTS:  push(LK::Artists, 0); break;
        case H_ALBUMS:   push(LK::Albums, 0); break;
        case H_SONGS:    push(LK::Songs, 0); break;
        case H_BT:       switchMode(Mode::BT); break;
        case H_SETTINGS: push(LK::Settings, 0); break;
      }
      break;
    case LK::Artists:      push(LK::ArtistAlbums, c); break;
    case LK::ArtistAlbums: push(LK::AlbumTracks, g_lib.artist(param).firstAlbum + c); break;
    case LK::Albums:       push(LK::AlbumTracks, c); break;
    case LK::AlbumTracks: {
      const AlbumRec& al = g_lib.album(param);
      g_player.send(PCmd::PlayRange, al.firstTrack, al.trackCount, c);
      sNowView = true;
      break;
    }
    case LK::Songs:
      g_player.send(PCmd::PlayRange, 0, g_lib.nTracks, c);
      sNowView = true;
      break;
    case LK::Settings: settingAction(c); break;
    case LK::About: break;
  }
}

static void listEvent(Ev e) {
  Nav& n = sNav[sSp];
  int cnt = (int)listCount(n);
  switch (e) {
    case Ev::CW:        if (cnt) n.cursor = (n.cursor + 1) % cnt; break;
    case Ev::CCW:       if (cnt) n.cursor = (n.cursor + cnt - 1) % cnt; break;
    case Ev::Click:     selectItem(); break;
    case Ev::Back:      if (sSp > 0) sSp--; break;
    case Ev::BackLong:  sSp = 0; break;
    case Ev::LongClick: if (g_player.status().trackSerial) sNowView = true; break;
    case Ev::Play:      g_player.send(PCmd::TogglePause); break;
    case Ev::PlayLong:  g_player.send(PCmd::Next); break;
    default: break;
  }
}

static void nowEvent(Ev e) {
  switch (e) {
    case Ev::CW:        volStep(+1); break;
    case Ev::CCW:       volStep(-1); break;
    case Ev::Click:
    case Ev::Play:      g_player.send(PCmd::TogglePause); break;
    case Ev::PlayLong:  g_player.send(PCmd::Next); break;
    case Ev::BackLong:  g_player.send(PCmd::Prev); break;
    case Ev::Back:
    case Ev::LongClick: sNowView = false; break;
    default: break;
  }
}

static void handleEvent(Ev e) {
  bool wasOff = screenPoke();
  sLastAct = millis();
  sDirty = true;
  if (wasOff) {                        // screen was dark: act as media keys, never navigate blind
    switch (e) {
      case Ev::CW:       volStep(+1); break;
      case Ev::CCW:      volStep(-1); break;
      case Ev::Play:     g_player.send(PCmd::TogglePause); break;
      case Ev::PlayLong: g_player.send(PCmd::Next); break;
      case Ev::BackLong: g_player.send(PCmd::Prev); break;
      default: break;
    }
    return;
  }
  uiMarqueeReset();
  if (sNowView) nowEvent(e); else listEvent(e);
}

// ---- boot ----------------------------------------------------------------------------
static void showMsg(const char* l1) {
  gCv.fillScreen(C_BG);
  gCv.setFont(UI_F16);
  uiCenterText(l1, gCv.height() / 2 - 8, C_FG);
  uiPush();
}

[[noreturn]] static void fatal(const char* l1, const char* l2) {
  gCv.fillScreen(C_BG);
  uiHeader("Error", -1, 0);
  gCv.setFont(UI_F20);
  uiCenterText(l1, 80, C_ACC);
  gCv.setFont(UI_F16);
  uiCenterText(l2, 116, C_FG);
  gCv.setFont(UI_F12);
  uiCenterText("Press encoder: Bluetooth mode", gCv.height() - 30, C_DIM);
  uiPush();
  screenPoke();
  for (;;) {
    if (inputPoll() == Ev::Click) switchMode(Mode::BT);
    delay(10);
  }
}

void dapSetup() {
  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);   // give the Bluetooth RAM back
  setCpuFrequencyMhz(CPU_MHZ);
  hwBegin();
  inputBegin();
  if (!uiInit()) Serial.println("[ui] back buffer allocation failed (is PSRAM enabled?)");
  showMsg("Starting...");

  if (!hwSdBegin()) fatal("SD card error", "Check card / wiring");
  showMsg("Loading library...");
  if (!g_lib.begin()) fatal("No music index", g_lib.error());

  if (gCfg.volume > VOLUME_MAX) gCfg.volume = VOLUME_MAX;
  g_player.begin();
  sNav[0] = {LK::Home, 0, 0, 0};
  sLastAct = millis();
  sDirty = true;
}

static void enterDeepSleep() {
  g_player.send(PCmd::Stop);
  dacMute(true);
  gLcd.setBrightness(0);
  settingsSave();
  delay(100);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_ENC_SW, 0);   // encoder button wakes the player
  esp_deep_sleep_start();
}

void dapLoop() {
  uint32_t now = millis();

  for (Ev e; (e = inputPoll()) != Ev::None;) handleEvent(e);

  const PlayerStatus& st = g_player.status();
  if (st.trackSerial != sSerial) {
    sSerial = st.trackSerial;
    if (st.trackSerial) loadCover(g_lib.track(st.track).albumIdx);
    uiMarqueeReset();
    sDirty = true;
  }
  if (st.state != sState || gCfg.volume != sVol || (sNowView && st.posSec != sPos)) sDirty = true;
  sState = st.state; sVol = gCfg.volume; sPos = st.posSec;

  if (sBattAt == 0 || now - sBattAt > 15000) { sBatt = batteryPercent(); sBattAt = now; sDirty = true; }

  if (sSleepAt && (int32_t)(now - sSleepAt) >= 0) {   // sleep timer
    sSleepAt = 0; sSleepIdx = 0;
    g_player.send(PCmd::Stop);
  }

  screenTick();
  settingsTick();

  if (st.state != PState::Playing && now - sLastAct > DEEP_SLEEP_IDLE_MS) enterDeepSleep();

  if (!screenIsOff() && (sDirty || (sAnim && now - sLastDraw > 40))) render();
  delay(4);
}
