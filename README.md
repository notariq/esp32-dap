# ESP32 DAP

A DIY digital audio player built on an **ESP32-WROVER**, a **PCM5102 DAC** and an **ST7789 IPS display**.

- **DAP mode:** plays MP3 / FLAC / WAV / AAC / M4A from an SD card, with a browsable library (Artists → Albums → Songs), cover art, shuffle/repeat, resume, sleep timer.
- **Bluetooth mode:** acts as an A2DP receiver for your phone, with track/artist/album from AVRCP and play/pause/next/previous sent back to the phone.

> **Status: untested on hardware.** The firmware was written against the library APIs but has not been compiled or flashed. The Python indexer *was* tested (see below). Expect to fix a few compile errors or API mismatches on first build. Section 9 lists the likeliest ones.

---

## 1. Hardware

| Part | Notes |
|---|---|
| ESP32-WROVER dev board | PSRAM is **required** (library index + screen buffer live there) |
| GY-PCM5102 | Solder pads: **1 = L, 2 = L, 3 = L, 4 = H** |
| ST7789 IPS TFT | 2.0" 240x320 is the default; see `config.h` for 1.69" / 1.54" |
| microSD slot / module | FAT32, card ≤ 32 GB works most reliably |
| EC11 rotary encoder with push switch | |
| 2 tactile buttons (PLAY, BACK) | |
| 3x 10 kΩ resistors | external pull-ups for encoder switch, PLAY, BACK |
| 100-470 µF + 0.1 µF capacitors | across the DAC supply |
| Optional: Li-ion cell + charger/regulator, 2x 10 kΩ divider for battery sense | |

## 2. Wiring

| Function | ESP32 GPIO |
|---|---|
| PCM5102 BCK / LCK / DIN | 26 / 25 / 22 |
| PCM5102 XSMT (optional, custom PCB) | 2 (set `PIN_DAC_XSMT`) |
| SD CS / SCK / MISO / MOSI (VSPI) | 5 / 18 / 19 / 23 |
| TFT SCLK / MOSI / DC / RST / BL / CS (HSPI) | 14 / 13 / 27 / 33 / 32 / 15 |
| Encoder A / B | 4 / 21 |
| Encoder switch | 35 + external 10k pull-up |
| PLAY button | 34 + external 10k pull-up |
| BACK button | 39 + external 10k pull-up |
| Battery sense (optional) | 36 via divider |

GPIO 34-39 are input-only and have **no internal pull-ups**. Buttons wired to them without a resistor will float and misbehave. Avoid GPIO 6-11 (flash), and 16/17 on WROVER boards (PSRAM).

## 3. Build and flash (PlatformIO)

```bash
pip install platformio
cd dap-esp32
pio run -t upload
pio device monitor
```

`platformio.ini` pins the Arduino-ESP32 **2.x** core (`espressif32@6.9.0`) and ESP32-audioI2S **2.x**. Do not bump to audioI2S 3.x or Arduino core 3.x without porting: the callback API and the I2S driver both changed.

Libraries used: LovyanGFX (display, sprites, built-in JPEG decoder and fonts, so TJpg_Decoder is not needed), ESP32-audioI2S (decoding), ESP32-A2DP (Bluetooth sink), ESP32Encoder (rotary encoder).

## 4. Prepare the SD card

1. Copy your music onto the card (any folder layout; tags are what matter).
2. Build the index on your PC:

```bash
pip install -r tools/requirements.txt
python tools/build_index.py /path/to/sdcard-root
```

This writes `/.dap/artists.bin`, `albums.bin`, `tracks.bin` and `/.dap/covers/*.jpg` (128x128 baseline JPEGs taken from embedded art or `cover.jpg` / `folder.jpg`). Re-run it whenever the music changes.

Tips:
- Grouping uses **album artist**, then artist. Tag compilations with an album artist such as "Various Artists" so they stay in one album.
- Missing tags fall back to folder names and file names.
- Paths longer than 155 bytes are skipped (the script tells you).
- Supported: MP3, FLAC, WAV, AAC, M4A (AAC only, not ALAC). M4A files should have the `moov` atom at the start for reliable playback.
- RAM use is about 256 bytes per track. 10,000 tracks is about 2.5 MB of PSRAM.

## 5. Controls

| Input | Browsing | Now Playing |
|---|---|---|
| Rotate | Move selection | Volume |
| Click | Select | Play / pause |
| Long click | Jump to Now Playing | Back to browsing |
| BACK | Go up one level | Back to browsing |
| BACK (hold) | Home | Previous track |
| PLAY | Play / pause | Play / pause |
| PLAY (hold) | Next track | Next track |

With the screen off, rotate / PLAY / PLAY-hold / BACK-hold work as media keys (volume, pause, next, previous) and everything else just wakes the screen.

**Bluetooth mode:** rotate = volume, click or PLAY = play/pause, PLAY-hold = next, BACK-hold = previous, **long click = back to DAP mode**. Enter it from *Home → Bluetooth Receiver*. The device then reboots into that mode and remembers it. Hold **BACK while powering on** to force DAP mode.

**Settings menu:** shuffle, repeat, brightness, screen timeout, sleep timer, about. After 5 minutes paused or stopped, the player enters deep sleep; the encoder button wakes it.

## 6. Configuration

Everything is in `src/config.h`: pins, display size and offsets, rotation, colour inversion and SPI mode, encoder detent size, CPU clock (160 MHz default; raise to 240 for hi-res FLAC), default and maximum volume, battery divider.

## 7. How it works

```
Core 1 (Arduino loop)           Core 0
  input polling                   audio task (priority 3)
  UI rendering (sprite + DMA)       command queue  <---- UI sends PlayRange/Next/Pause/...
  settings flush                    audio.loop()   (ESP32-audioI2S)
        |                           queue / shuffle / repeat / auto-advance
        +---- reads PlayerStatus <--- publishes position, track, format
```

- **One task owns the decoder.** The UI only posts commands and reads a status struct, so UI work never blocks decoding.
- **The index lives in RAM**, so browsing never touches the SD card. A single mutex guards SD access: the audio task holds it while decoding and the UI takes it only to read a cover JPEG.
- **Display and SD are on separate SPI buses** (HSPI and VSPI).
- **Modes are separate boots.** DAP mode releases Bluetooth RAM at start-up; Bluetooth mode never mounts the SD card.
- **Pop suppression:** if XSMT is wired, the DAC is muted at boot, between tracks, on pause and during mode changes.

### Index format (little endian, no header; counts come from file size)

| File | Record | Layout |
|---|---|---|
| `artists.bin` | 64 B | `u32 firstAlbum, u16 albumCount, u16 trackCount, char name[56]` |
| `albums.bin` | 128 B | `u32 artistIdx, u32 firstTrack, u16 trackCount, u16 year, u8 hasCover, u8 pad[3], char name[112]` |
| `tracks.bin` | 256 B | `u32 albumIdx, u32 artistIdx, u16 trackNo, u16 discNo, u32 durationSec, u16 year, u16 rsv, char title[80], char path[156]` |

Artists are sorted A-Z, albums by artist then year then name, tracks by album then disc and track number. Every parent's children are therefore one contiguous range. Cover for album *N* is `/.dap/covers/N.jpg`.

## 8. Project layout

```
platformio.ini
src/
  main.cpp        boot, mode selection
  config.h        pins and options
  display_cfg.h   LovyanGFX ST7789 setup
  hw.*            SD mount, DAC mute, battery, mode switch
  settings.*      NVS-backed settings
  input.*         encoder + buttons -> events
  library.*       index loader
  player.*        audio task, queue, shuffle/repeat
  ui_common.*     fonts, marquee, header, screen timeout
  ui_dap.*        DAP mode UI
  mode_bt.*       Bluetooth receiver mode
tools/
  build_index.py  PC-side indexer (mutagen + Pillow)
```

## 9. Known risks and troubleshooting

**Verified:** the indexer, including record sizes, sort order, contiguous ranges, UTF-8 truncation, cover extraction and tag fallbacks, was run against generated test files.

**Not verified:** compiling and running the firmware. Most likely trouble spots:

- **audioI2S API:** if the build complains about `connecttoFS`, `getAudioCurrentTime`, `getBitsPerSample` or the `audio_eof_mp3` hook, check that the 2.x tag in `platformio.ini` exists, or use the nearest 2.x release.
- **LovyanGFX font names:** if `lgfxJapanGothic_*` is missing in your version, swap the `UI_F*` macros in `ui_common.h` for `lgfx::fonts::Font2` / `Font4`. Non-Latin text will then not render.
- **Blank or garbled display:** try `TFT_SPI_MODE 3`, flip `TFT_INVERT`, and check `TFT_OFFSET_*` for 1.69" panels. Swapped red and blue means flip `TFT_RGB_ORDER`.
- **Boot loop or "back buffer allocation failed":** PSRAM is not enabled. You need a WROVER board and the `-DBOARD_HAS_PSRAM` flag.
- **Buttons act randomly:** missing external pull-ups on GPIO 34/35/39.
- **Silence or noise:** re-check the four solder pads on the PCM5102, add the supply capacitors, and keep I2S wires short.
- **Hiss:** see the noise notes from the design phase: clean DAC supply, series resistors on the I2S lines, an output attenuator for sensitive IEMs. A faint hiss on BT mode that disappears in DAP mode usually means supply sag during radio bursts.
- **Hi-res FLAC stutters:** raise `CPU_MHZ` to 240 and keep to 16-bit/44.1-48 kHz first.

## 10. Not implemented yet

Seek within a track, gapless playback, playlists (M3U), favorites, EQ, ReplayGain, on-device re-indexing, battery-percentage calibration UI. The finer-than-21-step volume needs a newer audioI2S API than the pinned 2.x. Until then, use a hardware attenuator for sensitive IEMs.
