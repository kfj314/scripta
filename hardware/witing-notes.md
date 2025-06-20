# Scripta Wiring Notes

This document describes the wiring connections for hardware components.

## E-Ink Display (Waveshare 7.5")

| E-Ink Pin | Raspberry Pi Pin | Notes |
|-----------|-------------------|-------|
| VCC | 3.3V (Pin 1) | Power |
| GND | GND (Pin 6) | Ground |
| DIN | GPIO 10 (MOSI, Pin 19) | SPI Data |
| CLK | GPIO 11 (SCLK, Pin 23) | SPI Clock |
| CS | GPIO 8 (CE0, Pin 24) | SPI Chip Select |
| DC | GPIO 25 (Pin 22) | Data/Command |
| RST | GPIO 17 (Pin 11) | Reset |
| BUSY | GPIO 24 (Pin 18) | Busy Indicator |

## Rotary Encoder

| Encoder Pin | Raspberry Pi Pin | Notes |
|-------------|-------------------|-------|
| A | GPIO 16 | Direction |
| B | GPIO 12 | Direction |
| SW | GPIO 6 | Push Button |

## NeoPixel Lightbar

- Controlled via single GPIO data pin with `adafruit-circuitpython-neopixel` library.

| NeoPixel Pin | Raspberry Pi Pin | Notes |
|---------------|-------------------|-------|
| DIN | GPIO 18 (PWM0) | Data In |
| VCC | 5V (Pin 2) | Power |
| GND | GND (Pin 6) | Ground |

---

## Notes

- Always verify pin mappings for your exact hardware model.
- Use level shifters if needed for 5V logic devices.
- Consider adding fuses or protection circuits for safety.
