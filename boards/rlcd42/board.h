#pragma once
// Waveshare ESP32-S3-RLCD-4.2: 400x300 reflective LCD (ST7305).

#define BOARD_NAME "ESP32-S3-RLCD-4.2"

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

// Buttons: BOOT = down (hold = up), KEY = select (hold = sleep)
#define PIN_BOOT 0
#define PIN_KEY 18
#define BTN_NAV_HINT "BOOT=down KEY=select"
#define BTN_BT_COMBO "Hold BOOT + KEY"
#define WAKE_HINT "press KEY to wake"

// Battery: ADC through a 3:1 divider
#define PIN_BAT 4

// I2C bus (RTC)
#define PIN_I2C_SDA 13
#define PIN_I2C_SCL 14
#define RTC_ADDR 0x51
