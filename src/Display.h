#pragma once

#include <LovyanGFX.hpp>

#include "config.h"

// ESP32-C3 Super Mini + ST7735S 128x128，SPI2_HOST
// 引脚与 esp32_screen_st7735 项目一致
class LGFX : public lgfx::LGFX_Device {
 public:
  LGFX() {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = TFT_SPI_HZ;
      cfg.freq_read = 16000000;
      cfg.pin_sclk = PIN_TFT_SCLK;
      cfg.pin_mosi = PIN_TFT_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc = PIN_TFT_DC;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = PIN_TFT_CS;
      cfg.pin_rst = PIN_TFT_RST;
      cfg.pin_busy = -1;
      cfg.memory_width = TFT_MEM_WIDTH;
      cfg.memory_height = TFT_MEM_HEIGHT;
      cfg.panel_width = TFT_WIDTH_PX;
      cfg.panel_height = TFT_HEIGHT_PX;
      cfg.offset_x = TFT_COL_OFFSET;
      cfg.offset_y = TFT_ROW_OFFSET;
      cfg.offset_rotation = 0;
      cfg.rgb_order = false;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = TFT_INVERT;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }

 private:
  lgfx::Panel_ST7735S _panel_instance;
  lgfx::Bus_SPI _bus_instance;
};

extern LGFX tft;
