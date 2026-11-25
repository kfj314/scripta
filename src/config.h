#pragma once
#include <Arduino.h>

#define SCRIPTA_VERSION "0.1.0"

// Panel (landscape after U8G2_R1)
#define LCD_W 400
#define LCD_H 300

// Display (ST7305 over SPI)
#define PIN_LCD_SCK 11
#define PIN_LCD_MOSI 12
#define PIN_LCD_DC 5
#define PIN_LCD_CS 40
#define PIN_LCD_RST 41

// microSD (SDMMC, 1-bit)
#define PIN_SD_CLK 38
#define PIN_SD_CMD 21
#define PIN_SD_D0 39

// Misc
#define PIN_BOOT 0
#define PIN_KEY 18
#define PIN_BAT 4
#define PIN_I2C_SDA 13
#define PIN_I2C_SCL 14
#define RTC_ADDR 0x51

// Where scripta keeps its own bookkeeping on the card
#define META_DIR "/.scripta"
#define FONT_DIR "/fonts"
