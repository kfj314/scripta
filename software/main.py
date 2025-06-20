# Scripta - Main Controller

import time
import board
import neopixel
import RPi.GPIO as GPIO

# --------- NeoPixel Lightbar Setup ---------
NEOPIXEL_PIN = board.D18
NUM_PIXELS = 8
BRIGHTNESS = 0.5

pixels = neopixel.NeoPixel(NEOPIXEL_PIN, NUM_PIXELS, brightness=BRIGHTNESS, auto_write=False)

# --------- Rotary Encoder Setup ---------
ENCODER_A = 16
ENCODER_B = 12
BUTTON = 6

GPIO.setmode(GPIO.BCM)
GPIO.setup(ENCODER_A, GPIO.IN, pull_up_down=GPIO.PUD_UP)
GPIO.setup(ENCODER_B, GPIO.IN, pull_up_down=GPIO.PUD_UP)
GPIO.setup(BUTTON, GPIO.IN, pull_up_down=GPIO.PUD_UP)

# Placeholder for brightness adjustment
current_brightness = BRIGHTNESS

def adjust_brightness(direction):
    global current_brightness
    step = 0.1
    if direction == "up" and current_brightness < 1.0:
        current_brightness += step
    elif direction == "down" and current_brightness > 0.0:
        current_brightness -= step
    current_brightness = max(0.0, min(1.0, current_brightness))
    pixels.brightness = current_brightness
    pixels.fill((255, 255, 255, 0))  # Warm white
    pixels.show()
    print(f"Brightness: {current_brightness:.1f}")

# --------- Main Loop ---------
try:
    print("Scripta is running...")
    while True:
        # Simple poll for demonstration
        if GPIO.input(ENCODER_A) == 0:
            adjust_brightness("up")
            time.sleep(0.2)
        if GPIO.input(ENCODER_B) == 0:
            adjust_brightness("down")
            time.sleep(0.2)
        if GPIO.input(BUTTON) == 0:
            print("Button pressed!")
            time.sleep(0.2)
except KeyboardInterrupt:
    print("Shutting down Scripta...")
finally:
    GPIO.cleanup()
