#pragma once
#include <Arduino.h>

// ============================================================================
//  ESP32 DAP - hardware & behaviour configuration
//  Edit this file to match your wiring, display and taste.
// ============================================================================

#define FW_VERSION      "0.1.0"
#define BT_DEVICE_NAME  "ESP32-DAP"

// ---- I2S -> PCM5102 --------------------------------------------------------
#define PIN_I2S_BCK     26
#define PIN_I2S_LRC     25
#define PIN_I2S_DIN     22
// DAC soft-mute (XSMT). -1 = not wired (GY-PCM5102 with solder pad 4 = H).
// Custom PCB: GPIO 2 with a pull-down, so the DAC stays muted until firmware
// releases it (removes boot / mode-switch pops).
#define PIN_DAC_XSMT    -1

// ---- SD card (VSPI, its own bus so UI redraws never stall audio reads) -----
#define PIN_SD_CS       5
#define PIN_SD_SCK      18
#define PIN_SD_MISO     19
#define PIN_SD_MOSI     23
#define SD_FREQ_HZ      25000000

// ---- ST7789 TFT (HSPI) ------------------------------------------------------
#define PIN_TFT_SCLK    14
#define PIN_TFT_MOSI    13
#define PIN_TFT_DC      27
#define PIN_TFT_RST     33
#define PIN_TFT_BL      32
#define PIN_TFT_CS      15      // -1 if your module has no CS pin (tie it low)
#define TFT_PANEL_W     240     // 2.0" = 240x320, 1.69" = 240x280, 1.54" = 240x240
#define TFT_PANEL_H     320
#define TFT_OFFSET_X    0       // 1.69" 240x280 modules usually need OFFSET_Y = 20
#define TFT_OFFSET_Y    0
#define TFT_ROTATION    0       // 0 = portrait
#define TFT_INVERT      true    // most IPS ST7789 modules need inversion on
#define TFT_RGB_ORDER   false   // flip if red and blue are swapped
#define TFT_SPI_MODE    0       // try 3 for modules without a CS pin
#define TFT_SPI_HZ      40000000

// ---- Controls --------------------------------------------------------------
// GPIO 34/35/36/39 are input-only and have NO internal pull-ups:
// every button on those pins needs an external 10k pull-up to 3V3.
#define PIN_ENC_A       4
#define PIN_ENC_B       21
#define PIN_ENC_SW      35      // must be an RTC GPIO if you want wake from deep sleep
#define PIN_BTN_PLAY    34
#define PIN_BTN_BACK    39
#define ENC_COUNTS_PER_DETENT 4 // EC11: 4 (full quad) or 2 depending on the part
#define ENC_INVERT      0
#define LONG_PRESS_MS   600

// ---- Battery ---------------------------------------------------------------
#define HAS_BATTERY_SENSE 0     // set to 1 once a divider is wired to PIN_BATT_ADC
#define PIN_BATT_ADC    36
#define BATT_DIV_RATIO  2.0f    // (R1+R2)/R2 of the divider

// ---- Behaviour -------------------------------------------------------------
#define CPU_MHZ         160     // raise to 240 if hi-res FLAC stutters
#define VOLUME_MAX      21      // audioI2S volume range 0..21
#define VOLUME_DEFAULT  6       // keep low: PCM5102 is ~2 Vrms and IEMs are sensitive
#define DEEP_SLEEP_IDLE_MS (5UL * 60UL * 1000UL)  // paused/stopped this long -> deep sleep
#define COVER_SIZE      128     // must match tools/build_index.py --cover-size
#define COVER_BUF_BYTES (48 * 1024)
#define INDEX_DIR       "/.dap"
