#pragma once

// ============================================================
// Pinout for the Xiaozhi/Spotpear "Ball V2" (ESP32-S3-DevKitC-1).
// Single source of truth for every GPIO used by the firmware.
// Source: https://github.com/RealDeco/xiaozhi-esphome (Ball_v2.yaml)
// ============================================================

// --- GC9A01 display (240x240 round, SPI) ---
#define PIN_LCD_SCLK      4
#define PIN_LCD_MOSI      2
#define PIN_LCD_CS        5
#define PIN_LCD_DC        47
#define PIN_LCD_RST       38
#define PIN_LCD_BACKLIGHT 42   // PWM, inverted logic (0 = brightest)

// --- CST816 touch (dedicated I2C, bus "B") ---
#define PIN_TOUCH_SDA     11
#define PIN_TOUCH_SCL     7
#define PIN_TOUCH_INT     12
#define PIN_TOUCH_RST     6

// --- I2C bus "A" (ES8311 audio codec control) ---
#define PIN_I2C_A_SDA     15
#define PIN_I2C_A_SCL     14

// --- I2S (audio data) ---
#define PIN_I2S_LRCLK     45
#define PIN_I2S_BCLK      9
#define PIN_I2S_MCLK      16
#define PIN_I2S_DIN       10  // microphone
#define PIN_I2S_DOUT      8   // speaker
#define PIN_SPEAKER_EN    46

// --- Misc ---
#define PIN_BOOT_BUTTON   0   // BOOT button, INPUT_PULLUP, active LOW
#define PIN_RGB_LED       48  // WS2812 (single LED)
#define PIN_BATTERY_ADC   1
