#pragma once
// Waveshare ESP32-S3-ePaper-3.97: 800x480 e-paper (SSD1677-family controller),
// TG28 (AXP2101-compatible) power chip, PCF85063 RTC, ES8311 audio codec.
// Pins from Waveshare's example code (github.com/waveshareteam/ESP32-S3-ePaper-3.97).

#define BOARD_NAME "ESP32-S3-ePaper-3.97"
#define BOARD_EPAPER 1

// Layouts are designed for 400x300; scale them by 480/300 = 1.6 (see src/layout.h)
#define LAYOUT_SCALE_NUM 8
#define LAYOUT_SCALE_DEN 5

// Panel (native landscape)
#define LCD_W 800
#define LCD_H 480

// Display (SPI). Powered from the PMIC's ALDO3 rail.
#define PIN_EPD_SCK 11
#define PIN_EPD_MOSI 12
#define PIN_EPD_CS 10
#define PIN_EPD_DC 9
#define PIN_EPD_RST 46
#define PIN_EPD_BUSY 3

// microSD (SDMMC, 4-bit)
#define SD_4BIT 1
#define PIN_SD_CLK 16
#define PIN_SD_CMD 17
#define PIN_SD_D0 15
#define PIN_SD_D1 7
#define PIN_SD_D2 8
#define PIN_SD_D3 18

// Buttons (active low). The jog wheel gives up / press / down.
#define HAS_JOG_WHEEL 1
// PWR is wired to the PMIC, not to a GPIO.
#define PIN_BOOT 0
#define PIN_BTN_UP 4
#define PIN_BTN_FN 5
#define PIN_BTN_DOWN 6
#define PIN_KEY PIN_BTN_FN  // "select" button, also wakes from sleep
#define BTN_NAV_HINT "Wheel=move, press=select, BOOT=back"
#define BTN_BT_COMBO "Hold wheel up + down"
#define WAKE_HINT "press the wheel to wake"

// I2C bus shared by RTC, PMIC, IMU, SHTC3 and the audio codec
#define PIN_I2C_SDA 41
#define PIN_I2C_SCL 42
#define RTC_ADDR 0x51
#define PIN_RTC_INT 45

// Audio (unused for now)
#define PIN_I2S_MCLK 13
#define PIN_I2S_BCLK 14
#define PIN_I2S_LRCK 47
#define PIN_I2S_DOUT 48
#define PIN_I2S_DIN 21
#define PIN_PA_EN 39  // Waveshare's IMU header also lists 39 (INT1); check the schematic before using
