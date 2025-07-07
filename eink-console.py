#!/usr/bin/env python3
import os
import time
from PIL import Image
import epd7in5_V2

epd = epd7in5_V2.EPD()
epd.init()
epd.Clear()

def grab_framebuffer():
    os.system("fbgrab /tmp/fbshot.png")

def convert_and_display():
    grab_framebuffer()
    image = Image.open('/tmp/fbshot.png').convert('1')
    image = image.resize((epd.width, epd.height))
    epd.display(epd.getbuffer(image))
    epd.sleep()

try:
    while True:
        convert_and_display()
        time.sleep(5)  # Adjust refresh interval if needed
except KeyboardInterrupt:
    print("Exiting...")
    epd.sleep()
