#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "pins.h"

// Display class for the Ball V2: round GC9A01 240x240 panel over SPI,
// with a CST816 capacitive touch controller on a dedicated I2C bus.
class LGFX_BallV2 : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;
  lgfx::Touch_CST816S _touch_instance;

public:
  LGFX_BallV2() {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = true;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_LCD_SCLK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc   = PIN_LCD_DC;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs   = PIN_LCD_CS;
      cfg.pin_rst  = PIN_LCD_RST;
      cfg.pin_busy = -1;
      cfg.panel_width  = 240;
      cfg.panel_height = 240;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.invert = true;   // the Ball V2's GC9A01A uses inverted colors
      cfg.rgb_order = false;
      cfg.readable = false;
      _panel_instance.config(cfg);
    }
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = PIN_LCD_BACKLIGHT;
      cfg.invert = true;   // backlight pin is inverted on the Ball V2
      cfg.freq   = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
    {
      auto cfg = _touch_instance.config();
      cfg.x_min = 0; cfg.x_max = 239;
      cfg.y_min = 0; cfg.y_max = 239;
      cfg.pin_int = PIN_TOUCH_INT;
      cfg.pin_rst = PIN_TOUCH_RST;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 1;   // I2C bus #1 (dedicated to the touch controller)
      cfg.i2c_addr = 0x15;
      cfg.pin_sda = PIN_TOUCH_SDA;
      cfg.pin_scl = PIN_TOUCH_SCL;
      cfg.freq = 400000;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }
    setPanel(&_panel_instance);
  }
};
