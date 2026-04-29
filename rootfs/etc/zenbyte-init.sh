#!/bin/bash

# ZenbyteOS System Init Script
# Läuft beim Boot nach systemd

GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}[ZenbyteOS]${NC} System wird initialisiert..."

# Internet Check
check_internet() {
  ping -c 1 -W 2 8.8.8.8 &>/dev/null && return 0 || return 1
}

# Netzwerk starten
echo -e "${CYAN}[ZenbyteOS]${NC} Netzwerk wird gestartet..."
for IFACE in $(ls /sys/class/net/ | grep -v lo); do
  ip link set $IFACE up 2>/dev/null
done

# Warte auf Internet
for i in {1..10}; do
  if check_internet; then
    echo -e "${GREEN}[ZenbyteOS]${NC} Internet verbunden."
    break
  fi
  sleep 1
done

# PulseAudio starten
if command -v pulseaudio &>/dev/null; then
  echo -e "${CYAN}[ZenbyteOS]${NC} Audio wird gestartet..."
  pulseaudio --start --daemonize 2>/dev/null
fi

# D-Bus starten
if command -v dbus-daemon &>/dev/null; then
  echo -e "${CYAN}[ZenbyteOS]${NC} D-Bus wird gestartet..."
  dbus-launch --system 2>/dev/null || true
fi

# zbpm update check
if check_internet; then
  echo -e "${CYAN}[ZenbyteOS]${NC} Prüfe Updates..."
  UPDATE_COUNT=$(zbpm update --check 2>/dev/null | grep -c "verfügbar" || echo "0")
  if [ "$UPDATE_COUNT" -gt "0" ]; then
    echo -e "${GREEN}[ZenbyteOS]${NC} $UPDATE_COUNT Updates verfügbar. Installieren mit: zbpm update"
    # Notification falls XFCE läuft
    notify-send "ZenbyteOS Updates" "$UPDATE_COUNT Updates verfügbar" 2>/dev/null || true
  fi
fi

echo -e "${GREEN}[ZenbyteOS]${NC} System bereit."
