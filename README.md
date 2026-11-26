# Scripta

Writerdeck firmware for the Waveshare ESP32-S3-RLCD-4.2 (400x300 reflective LCD).
Markdown-first editor with a Bluetooth keyboard, microSD storage, per-document-type
formatting and Wi-Fi sync.

## Build and flash

```
python -m platformio run -t upload
python -m platformio device monitor
```

Run from PowerShell/cmd, not Git Bash (PlatformIO rejects MSys). If upload fails, hold BOOT while
plugging in USB. Pins and the ST7305 driver come from Waveshare's own Arduino demo (Apache-2.0).

## First boot

1. Put a FAT32 microSD card in. Without one it falls back to internal flash.
2. No keyboard is paired yet, so the buttons navigate menus: **BOOT** = down (hold = up), **KEY** = select.
3. Home > Bluetooth keyboard > Scan, put the keyboard in pairing mode, pick it. If it shows a 6-digit code,
   type it on the keyboard and press Enter. It reconnects automatically afterwards.
   Up to 3 keyboards are remembered; whichever one is switched on reconnects. Shortcut: hold **BOOT + KEY**
   for about 1.5 s to jump to the Bluetooth screen and start scanning (works with a keyboard connected).
4. Home > New file. Choose Novel chapter, Note, Journal, Outline, To-do or Plain text. The type decides font,
   indent, spacing and templates, and is remembered per file in `/.scripta/types.tsv`.

## Writing

Live-styled markdown: `#`, `-`, `1.`, `**bold**` and `_italic_` markers are hidden except on the line the cursor is on.
Headings H1-H3, bullets (2 levels), numbered lists that renumber themselves, `- [ ]` checkboxes, `---` rules.
Enter continues lists; Enter on an empty item ends the list. Novel preset: indented first lines, no gap, centered H1.

| Keys | Action |
|---|---|
| Esc | Menu |
| Ctrl+S / O / N | Save / open files / new file |
| Ctrl+Z / Y | Undo / redo |
| Ctrl+B / I | Bold / italic word |
| Ctrl+1 2 3 / 0 | Heading / plain |
| Ctrl+L / K | Bullets / numbers |
| Ctrl+Enter | Tick checkbox |
| Tab / Shift+Tab | Indent / outdent list item |
| Ctrl+= / Ctrl+- | Text size |
| Ctrl+F | Next font |
| Ctrl+D | Light / dark |
| Ctrl+G | Word goal |
| Ctrl+R, F5 | Sync |
| Ctrl+Q, hold KEY | Sleep |
| Hold BOOT + KEY | Bluetooth screen + scan |

Cmd works as Ctrl. Autosave runs a few seconds after you stop typing (writes are power-loss safe).
Status bar: file, word count and session goal progress, Bluetooth/Wi-Fi, clock, battery %.

## Fonts

Built in: Serif, Sans, Mono at six sizes (8-24 pt). Drop `.ttf` files in `/fonts` on the card, then
Settings > Rescan fonts. TTFs are rasterised to 1-bit (bold and italic are synthesised).
Typographic quotes, dashes and ellipses are folded to ASCII for the bitmap fonts.

## Sync

Syncthing cannot run on an ESP32, so the device speaks **WebDAV** to a small server on your home network
(ideally sharing a folder that Syncthing, Obsidian or similar keeps in step with your other computers).
Pick one:

- **Windows PC**: [Scripta Sync Server](tools/sync-server/README.md), a tray app with a start/stop window.
  Download this repo, double-click `tools/sync-server/Install.cmd`.
- **Raspberry Pi / Linux**: [always-on systemd service](tools/pi-server/README.md). `sudo ./setup.sh` sets it up.
- **Anything else with rclone**: `rclone serve webdav /path/to/folder --addr :8080 --user me --pass secret --dir-cache-time 1s`

Then on the device: Wi-Fi & sync > connect, and set:

| Setting | Value |
|---|---|
| Server URL | `http://<server-ip>:8080/<folder>`. The folder must already exist on the server. |
| User / Password | as shown by the server app or setup script |
| Folder to sync | the card folder to mirror, e.g. `/Scripta` (use one card folder per server folder) |

Ctrl+R syncs on demand; optional auto-sync runs when idle. It is two-way with a local state file; if both sides
changed a file, both are kept (`name.conflict.md`). Deletions propagate. Changing the Server URL or card folder
starts a fresh sync record, so nothing is deleted when you switch. HTTPS works but the certificate is not verified.
The server address is used as typed, so give the server a fixed IP (a DHCP reservation in your router).

Other transfer options: Wi-Fi > file transfer opens a browser file manager on the device's IP; the phone
hotspot (`Scripta-Setup`, password `scripta123`) serves a setup form for Wi-Fi and sync settings.

## Not verified on hardware

Written and compiled without the board in hand. Likely first-run adjustments:
- Pixel polarity: if the screen looks inverted, toggle Settings > Dark theme.
- BLE keyboards differ; this uses the boot-protocol report (8 bytes). Report logs from the serial monitor if one fails.
- Wake from deep sleep is by the KEY/BOOT buttons only (a BLE keyboard cannot wake it).
- Battery % uses a Li-ion voltage curve on the board's 3:1 divider; expect a few percent of error while charging.

Not implemented: text selection / copy-paste, find and replace, CPU frequency scaling.

## Repository layout

| Path | What |
|---|---|
| `src/`, `platformio.ini` | ESP32-S3 firmware (PlatformIO, Arduino framework) |
| `fonts/` | TTFs embedded in the firmware (SIL Open Font License, see `fonts/README.md`) |
| `docs/` | screenshots and a printable shortcut card |
| `tools/sync-server/` | Windows tray app that runs the sync server |
| `tools/pi-server/` | Raspberry Pi / Linux sync server setup |

The previous Python/Raspberry Pi e-ink version of Scripta is kept at the `legacy-python` tag.

## License

Scripta is released under the [MIT License](LICENSE). The third-party parts listed below keep their own licenses.

## Credits

- ST7305 display driver and board pins adapted from Waveshare's ESP32-S3-RLCD-4.2 Arduino demo (Apache-2.0).
- `src/stb_truetype.h` by Sean Barrett (public domain / MIT).
- Averia Serif Libre and Quicksand fonts (SIL Open Font License 1.1).
- Sync server built on [rclone](https://rclone.org) (MIT), installed separately.
