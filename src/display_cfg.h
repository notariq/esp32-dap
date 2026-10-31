#pragma once
#ifndef LGFX_USE_V1
#define LGFX_USE_V1
#endif
#include <LovyanGFX.hpp>
#include "config.h"

// LovyanGFX device description for an ST7789 on the HSPI bus.
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI      _bus;
  lgfx::Light_PWM    _light;

 public:
  LGFX() {
    {
      auto c = _bus.config();
      c.spi_host    = SPI2_HOST;          // HSPI; SD card uses VSPI
      c.spi_mode    = TFT_SPI_MODE;
      c.freq_write  = TFT_SPI_HZ;
      c.freq_read   = 16000000;
      c.spi_3wire   = true;
      c.use_lock    = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk    = PIN_TFT_SCLK;
      c.pin_mosi    = PIN_TFT_MOSI;
      c.pin_miso    = -1;
      c.pin_dc      = PIN_TFT_DC;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    {
      auto c = _panel.config();
      c.pin_cs       = PIN_TFT_CS;
      c.pin_rst      = PIN_TFT_RST;
      c.pin_busy     = -1;
      c.panel_width  = TFT_PANEL_W;
      c.panel_height = TFT_PANEL_H;
      c.offset_x     = TFT_OFFSET_X;
      c.offset_y     = TFT_OFFSET_Y;
      c.invert       = TFT_INVERT;
      c.rgb_order    = TFT_RGB_ORDER;
      c.readable     = false;
      c.bus_shared   = false;
      _panel.config(c);
    }
    {
      auto c = _light.config();
      c.pin_bl      = PIN_TFT_BL;
      c.invert      = false;
      c.freq        = 44100;              // above the audible range: no backlight whine
      c.pwm_channel = 7;
      _light.config(c);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

extern LGFX gLcd;
