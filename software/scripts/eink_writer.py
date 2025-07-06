import time
import hashlib
from PIL import Image, ImageDraw, ImageFont

# Import your Waveshare e-ink module
import epd7in5_V2  # ← Change to match your display (e.g., epd2in13_V3)

WATCH_FILE = "/home/pi/notes/my_note.txt"

epd = epd7in5_V2.EPD()

def render_text_to_eink(text):
    epd.init()
    epd.Clear()

    image = Image.new('1', (epd.width, epd.height), 255)  # White background
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default()

    # Wrap text
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
            break

    epd.display(epd.getbuffer(image))
    epd.sleep()

def file_checksum(path):
    """Return MD5 checksum of file contents."""
    try:
        with open(path, 'rb') as f:
            data = f.read()
            return hashlib.md5(data).hexdigest()
    except FileNotFoundError:
        return None

if __name__ == "__main__":
    epd.init()
    epd.Clear()

    last_checksum = file_checksum(WATCH_FILE)

    # Initial render
    if last_checksum:
        with open(WATCH_FILE, "r") as f:
            text = f.read()
        render_text_to_eink(text)
    else:
        render_text_to_eink("File not found.")

    try:
        while True:
            current_checksum = file_checksum(WATCH_FILE)
            if current_checksum != last_checksum:
                print("File changed, updating display...")
                with open(WATCH_FILE, "r") as f:
                    text = f.read()
                render_text_to_eink(text)
                last_checksum = current_checksum

            time.sleep(2)  # Check every 2 seconds

    except KeyboardInterrupt:
        print("Stopped by user.")
        epd.sleep()
