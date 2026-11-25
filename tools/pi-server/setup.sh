#!/usr/bin/env bash
# Scripta sync server setup for Raspberry Pi OS / Debian / Ubuntu.
# Installs rclone, runs `rclone serve webdav` as a systemd service on a folder of your choice,
# and (optionally) installs Syncthing so that folder stays in step with your other computers.
#
#   sudo ./setup.sh                 interactive setup (safe to re-run to change settings)
#   sudo ./setup.sh --syncthing     also install and enable Syncthing for your user
#   sudo ./setup.sh --status        show service state and the device URL
#   sudo ./setup.sh --uninstall     remove the service and its settings (your files are kept)
#
# Non-interactive: SCRIPTA_FOLDER=... SCRIPTA_PORT=8080 SCRIPTA_USER=scripta SCRIPTA_PASS=... sudo -E ./setup.sh -y
set -euo pipefail

SERVICE=scripta-webdav
UNIT=/etc/systemd/system/$SERVICE.service
ENVFILE=/etc/scripta-webdav.env
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

say()  { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mxx\033[0m %s\n' "$*" >&2; exit 1; }

ASSUME_YES=0; WANT_SYNCTHING=0; MODE=install
for a in "$@"; do
  case "$a" in
    -y|--yes) ASSUME_YES=1 ;;
    --syncthing) WANT_SYNCTHING=1 ;;
    --status) MODE=status ;;
    --uninstall) MODE=uninstall ;;
    -h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "Unknown option: $a (see --help)" ;;
  esac
done

[ "$(id -u)" -eq 0 ] || die "Run with sudo: sudo $0 $*"
RUN_USER="${SUDO_USER:-}"
[ -n "$RUN_USER" ] && [ "$RUN_USER" != root ] || die "Run this with sudo from your normal user account (not as root directly)."
RUN_HOME="$(getent passwd "$RUN_USER" | cut -d: -f6)"

lan_ip() { hostname -I 2>/dev/null | awk '{print $1}'; }

# the subnet of the interface that carries the default route, e.g. 192.168.1.0/24
lan_subnet() {
  local dev; dev="$(ip -4 route show default 2>/dev/null | awk '{print $5; exit}')"
  [ -n "$dev" ] && ip -4 route show dev "$dev" scope link 2>/dev/null | awk '{print $1; exit}'
}

ask() {  # ask "Prompt" default -> echoes answer
  local reply
  if [ "$ASSUME_YES" -eq 1 ]; then echo "$2"; return; fi
  read -r -p "$1 [$2]: " reply </dev/tty || true
  echo "${reply:-$2}"
}

make_password() {
  local words=(apple river candle forest pebble garden lantern meadow harbor violet cobalt maple otter sparrow
    willow copper ember thistle canyon juniper orchid falcon marble saffron cedar glacier basil comet harvest island
    velvet walnut breeze clover dune fern hazel indigo jasper kettle lemon mango nutmeg olive pepper quartz raven
    sage tulip umber wren yarrow zephyr acorn badger cinder delta echo fable grove)
  local out="" i n
  for i in 1 2 3 4; do
    n=$(( $(od -An -N2 -tu2 /dev/urandom | tr -d ' ') % ${#words[@]} ))
    out="${out:+$out-}${words[$n]}"
  done
  echo "$out"
}

show_status() {
  local port=8080
  [ -f "$ENVFILE" ] && port="$(. "$ENVFILE"; echo "${SCRIPTA_PORT:-8080}")"
  systemctl --no-pager --lines=5 status "$SERVICE" || true
  echo
  echo "Device Server URL:  http://$(lan_ip):$port/<folder>"
  [ -f "$ENVFILE" ] && echo "Settings/password:  sudo cat $ENVFILE"
}

if [ "$MODE" = status ]; then show_status; exit 0; fi

if [ "$MODE" = uninstall ]; then
  say "Removing $SERVICE"
  systemctl disable --now "$SERVICE" 2>/dev/null || true
  rm -f "$UNIT" "$ENVFILE"
  systemctl daemon-reload
  if command -v ufw >/dev/null && ufw status 2>/dev/null | grep -q "Status: active"; then
    ufw status numbered | grep -q "Scripta WebDAV" && warn "A ufw rule tagged 'Scripta WebDAV' remains; remove it with: sudo ufw status numbered && sudo ufw delete <n>"
  fi
  say "Done. Your files and rclone itself were left in place."
  exit 0
fi

# ---------------------------------------------------------------- settings
OLD_FOLDER=""; OLD_PORT=""; OLD_USER=""; OLD_PASS=""
if [ -f "$ENVFILE" ]; then
  # shellcheck disable=SC1090
  OLD_FOLDER="$(. "$ENVFILE"; echo "${SCRIPTA_FOLDER:-}")"
  OLD_PORT="$(. "$ENVFILE"; echo "${SCRIPTA_PORT:-}")"
  OLD_USER="$(. "$ENVFILE"; echo "${RCLONE_USER:-}")"
  OLD_PASS="$(. "$ENVFILE"; echo "${RCLONE_PASS:-}")"
  say "Existing settings found; press Enter to keep each one."
fi

FOLDER="${SCRIPTA_FOLDER:-$(ask "Folder to share with the Scripta device" "${OLD_FOLDER:-$RUN_HOME/Scripta}")}"
PORT="${SCRIPTA_PORT:-$(ask "Port" "${OLD_PORT:-8080}")}"
DAV_USER="${SCRIPTA_USER:-$(ask "WebDAV user name" "${OLD_USER:-scripta}")}"
if [ -n "${SCRIPTA_PASS:-}" ]; then DAV_PASS="$SCRIPTA_PASS"
else
  DEFAULT_PASS="${OLD_PASS:-$(make_password)}"
  DAV_PASS="$(ask "WebDAV password (Enter accepts the suggested one)" "$DEFAULT_PASS")"
fi

[[ "$PORT" =~ ^[0-9]+$ ]] && [ "$PORT" -ge 1024 ] && [ "$PORT" -le 65535 ] || die "Port must be a number from 1024 to 65535."
[[ "$DAV_USER" =~ ^[A-Za-z0-9._-]+$ ]] || die "User name may only contain letters, digits, . _ -"
[ -n "$DAV_PASS" ] || die "Password cannot be empty."
case "$DAV_PASS$FOLDER" in *\"*|*\\*|*$'\n'*|*\$*|*\`*) die "Folder and password may not contain \" \\ \$ \` or newlines." ;; esac

if [ ! -d "$FOLDER" ]; then
  say "Creating $FOLDER"
  install -d -o "$RUN_USER" -g "$(id -gn "$RUN_USER")" "$FOLDER"
fi
sudo -u "$RUN_USER" test -w "$FOLDER" || die "$RUN_USER cannot write to $FOLDER"

# ---------------------------------------------------------------- packages
if ! command -v rclone >/dev/null; then
  say "Installing rclone"
  apt-get update -qq
  apt-get install -y rclone
fi
RCLONE="$(command -v rclone)"
say "Using $("$RCLONE" version | head -1) at $RCLONE"

if [ "$WANT_SYNCTHING" -eq 1 ]; then
  if ! command -v syncthing >/dev/null; then
    say "Installing Syncthing"
    apt-get update -qq
    apt-get install -y syncthing
  fi
  systemctl enable --now "syncthing@$RUN_USER"
fi

# ---------------------------------------------------------------- service
say "Writing $ENVFILE (readable by root only)"
umask 077
cat > "$ENVFILE" <<EOF
# Scripta WebDAV sync server settings. Re-run setup.sh to change them, or edit and
# run: sudo systemctl restart $SERVICE
SCRIPTA_FOLDER="$FOLDER"
SCRIPTA_PORT=$PORT
RCLONE_USER="$DAV_USER"
RCLONE_PASS="$DAV_PASS"
EOF
chmod 600 "$ENVFILE"; chown root:root "$ENVFILE"
umask 022

say "Installing $UNIT"
[ -f "$HERE/scripta-webdav.service" ] || die "scripta-webdav.service must be next to setup.sh"
sed -e "s|__USER__|$RUN_USER|g" -e "s|__RCLONE__|$RCLONE|g" "$HERE/scripta-webdav.service" > "$UNIT"
systemctl daemon-reload
systemctl enable "$SERVICE" >/dev/null
systemctl restart "$SERVICE"

# ---------------------------------------------------------------- firewall
if command -v ufw >/dev/null && ufw status 2>/dev/null | grep -q "Status: active"; then
  SUBNET="$(lan_subnet || true)"
  if [ -n "$SUBNET" ]; then
    say "Allowing port $PORT from $SUBNET in ufw"
    ufw allow from "$SUBNET" to any port "$PORT" proto tcp comment "Scripta WebDAV" >/dev/null
  else
    warn "ufw is active but the local subnet could not be found; allow port $PORT yourself."
  fi
fi

# ---------------------------------------------------------------- check
ok=0
for _ in $(seq 1 20); do
  code="$(curl -s -o /dev/null -w '%{http_code}' -u "$DAV_USER:$DAV_PASS" -X PROPFIND -H 'Depth: 0' "http://127.0.0.1:$PORT/" 2>/dev/null || true)"
  if [ "$code" = 207 ]; then ok=1; break; fi
  sleep 0.5
done
if [ "$ok" -ne 1 ]; then
  journalctl -u "$SERVICE" --no-pager -n 20 || true
  die "The server did not answer on port $PORT. See the log above."
fi

IP="$(lan_ip)"
cat <<EOF

$(say "Scripta sync server is running and starts at boot.")

  On the Scripta device: Wi-Fi & sync
    Server URL  http://$IP:$PORT/<folder inside $FOLDER>   (or just http://$IP:$PORT)
    User        $DAV_USER
    Password    $DAV_PASS

  Tip: give this Pi a fixed address (a DHCP reservation in your router) so the URL never changes.
  Status:    sudo $0 --status          Logs: journalctl -u $SERVICE -f
EOF
if [ "$WANT_SYNCTHING" -eq 1 ]; then
cat <<EOF

  Syncthing is running for $RUN_USER. Its web page only listens on the Pi itself; from your PC run
    ssh -L 8384:localhost:8384 $RUN_USER@$IP
  then open http://localhost:8384 to pair it with your other computers and share $FOLDER.
EOF
fi
