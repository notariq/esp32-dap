#include "player.h"
#include "hw.h"
#include "library.h"
#include "settings.h"
#include <Audio.h>
#include <SD.h>
#include <vector>

// All audio calls happen inside one FreeRTOS task. The UI never touches the
// decoder: it posts commands to a queue and reads the status struct.

struct Msg { PCmd type; uint32_t a, b, c; };

static Audio         sAudio;
static QueueHandle_t sQueue = nullptr;
static PlayerStatus  sSt;
Player               g_player;

static volatile bool sEof = false;
// ESP32-audioI2S 2.x calls these (weak) hooks.
void audio_eof_mp3(const char*) { sEof = true; }
void audio_info(const char* info) { Serial.printf("[audio] %s\n", info); }

// ---- queue (owned by the audio task) ---------------------------------------
static uint32_t              sQBase = 0, sQCount = 0, sQPos = 0;
static std::vector<uint32_t> sQOrder;
static bool                  sShuffle = false;
static Repeat                sRepeat  = Repeat::Off;
static bool                  sUnmutePending = false;
static uint32_t              sUnmuteAt = 0, sStartedAt = 0;

static void buildOrder(uint32_t startOffset) {
  sQOrder.resize(sQCount);
  for (uint32_t i = 0; i < sQCount; i++) sQOrder[i] = i;
  if (!sShuffle) { sQPos = startOffset < sQCount ? startOffset : 0; return; }
  for (uint32_t i = sQCount - 1; i > 0; i--) {
    uint32_t j = esp_random() % (i + 1);
    std::swap(sQOrder[i], sQOrder[j]);
  }
  for (uint32_t i = 0; i < sQCount; i++)        // current song goes first
    if (sQOrder[i] == startOffset) { std::swap(sQOrder[0], sQOrder[i]); break; }
  sQPos = 0;
}

static void startCurrent(int dir) {
  for (int tries = 0; tries < 8 && sQCount; tries++) {
    uint32_t ti = sQBase + sQOrder[sQPos];
    const TrackRec& t = g_lib.track(ti);
    dacMute(true);
    if (sAudio.connecttoFS(SD, t.path)) {
      sSt.track       = ti;
      sSt.trackSerial = sSt.trackSerial + 1;
      sSt.posSec      = 0;
      sSt.durSec      = t.durationSec;
      sSt.sampleRate  = 0;
      sSt.bits        = 0;
      sSt.qPos        = sQPos;
      sSt.qLen        = sQCount;
      sSt.state       = PState::Playing;
      sStartedAt      = millis();
      sUnmuteAt       = sStartedAt + 80;
      sUnmutePending  = true;
      gCfg.lastBase   = sQBase;
      gCfg.lastCount  = sQCount;
      gCfg.lastOffset = sQOrder[sQPos];
      settingsDirty();
      return;
    }
    Serial.printf("[player] cannot open %s\n", t.path);
    sQPos = (dir >= 0) ? (sQPos + 1) % sQCount : (sQPos + sQCount - 1) % sQCount;
  }
  sSt.state = PState::Stopped;
  dacMute(true);
}

static bool advance(int dir, bool wrap) {
  int64_t np = (int64_t)sQPos + dir;
  if (np >= (int64_t)sQCount)  { if (!wrap) return false; np = 0; }
  else if (np < 0)             { if (!wrap) return false; np = (int64_t)sQCount - 1; }
  sQPos = (uint32_t)np;
  startCurrent(dir);
  return sSt.state == PState::Playing;
}

static void onTrackEnd() {
  if (sSt.state != PState::Playing) return;
  if (sRepeat == Repeat::One) { startCurrent(+1); return; }
  if (!advance(+1, sRepeat == Repeat::All)) {
    sAudio.stopSong();
    sSt.state = PState::Stopped;
    dacMute(true);
  }
}

static void handle(const Msg& m) {
  switch (m.type) {
    case PCmd::PlayRange:
      if (m.b == 0 || m.a + m.b > g_lib.nTracks) break;
      sQBase = m.a; sQCount = m.b; sShuffle = gCfg.shuffle;
      buildOrder(m.c);
      startCurrent(+1);
      break;

    case PCmd::TogglePause:
      if (sSt.state == PState::Playing) {
        sAudio.pauseResume();
        dacMute(true);
        sSt.state = PState::Paused;
      } else if (sSt.state == PState::Paused) {
        sAudio.pauseResume();
        dacMute(false);
        sSt.state = PState::Playing;
      } else if (sQCount) {
        startCurrent(+1);                     // stopped: restart current song
      }
      break;

    case PCmd::Next:
      if (sQCount) advance(+1, true);
      break;

    case PCmd::Prev:
      if (!sQCount) break;
      if (sSt.posSec > 3) startCurrent(+1);   // restart instead of going back
      else advance(-1, true);
      break;

    case PCmd::SetVolume: {
      uint8_t v = m.a > VOLUME_MAX ? VOLUME_MAX : (uint8_t)m.a;
      sAudio.setVolume(v);
      gCfg.volume = v;
      settingsDirty();
      break;
    }

    case PCmd::SetShuffle: {
      sShuffle = m.a != 0;
      gCfg.shuffle = sShuffle;
      if (sQCount) buildOrder(sQOrder[sQPos]);
      settingsDirty();
      break;
    }

    case PCmd::SetRepeat:
      sRepeat = (Repeat)m.a;
      gCfg.repeat = sRepeat;
      settingsDirty();
      break;

    case PCmd::Stop:
      sAudio.stopSong();
      sSt.state = PState::Stopped;
      dacMute(true);
      break;
  }
}

static void audioTask(void*) {
  uint32_t lastStat = 0;
  for (;;) {
    xSemaphoreTake(g_sdMutex, portMAX_DELAY);

    Msg m;
    while (xQueueReceive(sQueue, &m, 0) == pdTRUE) handle(m);

    sAudio.loop();

    uint32_t now = millis();
    if (sEof) { sEof = false; onTrackEnd(); }
    // decoder gave up without an EOF callback (corrupt / unsupported file)
    if (sSt.state == PState::Playing && !sAudio.isRunning() && now - sStartedAt > 1500) onTrackEnd();

    if (sUnmutePending && (int32_t)(now - sUnmuteAt) >= 0) {
      sUnmutePending = false;
      if (sSt.state == PState::Playing) dacMute(false);
    }

    if (now - lastStat > 250) {
      lastStat = now;
      if (sSt.state != PState::Stopped) {
        sSt.posSec     = sAudio.getAudioCurrentTime();
        uint32_t d     = sAudio.getAudioFileDuration();
        if (d) sSt.durSec = d;
        sSt.sampleRate = sAudio.getSampleRate();
        sSt.bits       = sAudio.getBitsPerSample();
      }
    }

    xSemaphoreGive(g_sdMutex);
    vTaskDelay(sSt.state == PState::Playing ? 1 : 10);   // always let IDLE0 run
  }
}

void Player::begin() {
  sShuffle = gCfg.shuffle;
  sRepeat  = gCfg.repeat;
  sQueue   = xQueueCreate(16, sizeof(Msg));
  sAudio.setPinout(PIN_I2S_BCK, PIN_I2S_LRC, PIN_I2S_DIN);
  sAudio.setVolume(gCfg.volume);
  xTaskCreatePinnedToCore(audioTask, "audio", 12288, nullptr, 3, nullptr, 0);
}

void Player::send(PCmd c, uint32_t a, uint32_t b, uint32_t cc) {
  Msg m{c, a, b, cc};
  xQueueSend(sQueue, &m, 0);
}

const PlayerStatus& Player::status() const { return sSt; }
