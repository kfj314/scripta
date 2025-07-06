import sys
import time
import threading
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler
from PIL import Image, ImageDraw, ImageFont

# Import your Waveshare e-ink module
import epd7in5_V2  # Adjust to your display model

WATCH_FILE = "/home/pi/notes/my_note.txt"

epd = epd7in5_V2.EPD()

def render_text_to_eink(text):
    epd.init()
    epd.Clear()

    # Create a blank image for drawing
    image = Image.new('1', (epd.width, epd.height), 255)  # 1: black/white mode
    draw = ImageDraw.Draw(image)

    # Load a font
    font = ImageFont.load_default()
    
    # Wrap text manually
    lines = []
    words = text.split()
    line = ""
    for word in words:
        test_line = f"{line} {word}".strip()
        if draw.textlength(test_line, font=font) < epd.width - 20:
            line = test_line
        else:
            lines.append(line)
            line = word
    lines.append(line)

    y = 10
    for l in lines:
        draw.text((10, y), l, font=font, fill=0)
        y += 15
        if y > epd.height - 20:
            break  # Avoid overflow

    epd.display(epd.getbuffer(image))
    epd.sleep()

class FileChangeHandler(FileSystemEventHandler):
    def on_modified(self, event):
        if event.src_path == WATCH_FILE:
            with open(WATCH_FILE, "r") as f:
                text = f.read()
            render_text_to_eink(text)

def monitor_file():
    event_handler = FileChangeHandler()
    observer = Observer()
    observer.schedule(event_handler, path="/home/pi/notes", recursive=False)
    observer.start()
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        observer.stop()
    observer.join()

if __name__ == "__main__":
    # Clear display first
    epd.init()
    epd.Clear()

    # Initial render
    try:
        with open(WATCH_FILE, "r") as f:
            text = f.read()
        render_text_to_eink(text)
    except FileNotFoundError:
        render_text_to_eink("File not found.")

    monitor_file()
