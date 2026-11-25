# Scripta sync server on a Raspberry Pi (or any Debian/Ubuntu machine)

An always-on, low-power alternative to the Windows app: the Pi runs `rclone serve webdav` as a systemd
service, so the Scripta can sync even when your PC is off. Add Syncthing and the Pi keeps that folder in
step with your PC, laptop and phone.

Any Pi with Wi-Fi or Ethernet works, including a Pi Zero 2 W, on Raspberry Pi OS (Bookworm or later).

## Install

On the Pi (over SSH or at its desktop):

```bash
sudo apt install -y git
git clone https://github.com/kfj314/scripta.git
cd scripta/tools/pi-server
chmod +x setup.sh
sudo ./setup.sh --syncthing      # leave out --syncthing if you don't want it
```

It asks for:

| Prompt | Default |
|---|---|
| Folder to share | `~/Scripta` (created if missing) |
| Port | `8080` |
| User name | `scripta` |
| Password | a random four-word password (press Enter to accept) |

and then installs rclone, starts the `scripta-webdav` service (it also starts at every boot), opens the port in
`ufw` for your local network if ufw is enabled, checks that the server answers, and prints what to type into the
Scripta.

Run `sudo ./setup.sh` again at any time to change the folder, port or password.

## Connect the device

On the Scripta: **Wi-Fi & sync**

- **Server URL**: `http://<pi-ip>:8080/<folder>`, e.g. `http://192.168.1.30:8080/Notes`. The folder must exist inside the shared folder.
- **User / Password**: as printed by the setup script.
- **Folder to sync**: the card folder to mirror, e.g. `/Scripta`.

Give the Pi a fixed address (a DHCP reservation in your router). The device connects by IP.

## Syncthing

With `--syncthing`, Syncthing runs as your user. Its web page listens only on the Pi, so open it through SSH:

```bash
ssh -L 8384:localhost:8384 <user>@<pi-ip>      # run on your PC, keep it open
```

Then browse to http://localhost:8384, add your PC as a remote device, and share the folder you gave `setup.sh`.

## Day to day

```bash
sudo ./setup.sh --status                 # running? which URL?
journalctl -u scripta-webdav -f          # live log
sudo systemctl restart scripta-webdav    # restart
sudo ./setup.sh --uninstall              # remove the service (files are kept)
```

Settings and the password are in `/etc/scripta-webdav.env`, readable only by root. The password is passed to
rclone through the environment, so it never shows up in `ps`.

The server is plain HTTP, meant for your home network. Don't forward the port on your router.

## Files

| File | |
|---|---|
| `setup.sh` | installer / updater / uninstaller |
| `scripta-webdav.service` | systemd unit template (`setup.sh` fills in the user and rclone path) |
