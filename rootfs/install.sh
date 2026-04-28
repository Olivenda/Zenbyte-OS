#!/bin/bash

# ============================================================
#  ZenbyteOS Installer v2.0
# ============================================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
BOLD='\033[1m'
NC='\033[0m'

clear

# Header
echo -e "${CYAN}${BOLD}"
cat << 'LOGO'
  ███████╗███████╗███╗   ██╗██████╗ ██╗   ██╗████████╗███████╗
  ╚════██║██╔════╝████╗  ██║██╔══██╗╚██╗ ██╔╝╚══██╔══╝██╔════╝
      ██╔╝█████╗  ██╔██╗ ██║██████╔╝ ╚████╔╝    ██║   █████╗
     ██╔╝ ██╔══╝  ██║╚██╗██║██╔══██╗  ╚██╔╝     ██║   ██╔══╝
     ██║  ███████╗██║ ╚████║██████╔╝   ██║      ██║   ███████╗
     ╚═╝  ╚══════╝╚═╝  ╚═══╝╚═════╝    ╚═╝      ╚═╝   ╚══════╝
LOGO
echo -e "${NC}"
echo -e "  ${CYAN}ZenbyteOS Installer v2.0${NC}"
echo -e "  ${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "  Willkommen! Dieser Installer richtet ZenbyteOS ein."
echo ""

# System Info anzeigen
echo -e "${BOLD}${BLUE}[ System Information ]${NC}"
echo -e "  ${GREEN}•${NC} CPU:    $(grep 'model name' /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)"
echo -e "  ${GREEN}•${NC} RAM:    $(free -h | awk '/^Mem:/{print $2}')"
echo -e "  ${GREEN}•${NC} Kernel: $(uname -r)"
echo -e "  ${GREEN}•${NC} Arch:   $(uname -m)"
echo ""

read -p "$(echo -e "${YELLOW}Installation starten? [j/N]:${NC} ")" START
if [[ "$START" != "j" && "$START" != "J" ]]; then
  echo -e "${CYAN}Abgebrochen.${NC}"
  exit 0
fi

echo ""
echo -e "${BOLD}${BLUE}[ Sprache / Language ]${NC}"
echo -e "  ${GREEN}1)${NC} Deutsch"
echo -e "  ${GREEN}2)${NC} English"
read -p "$(echo -e "${YELLOW}Wahl [1/2]:${NC} ")" LANG_CHOICE
[ "$LANG_CHOICE" = "2" ] && LANG="en" || LANG="de"
echo ""

# Hostname
echo -e "${BOLD}${BLUE}[ Hostname ]${NC}"
read -p "$(echo -e "${YELLOW}Hostname eingeben [zenbyte]:${NC} ")" HOSTNAME
HOSTNAME=${HOSTNAME:-zenbyte}
echo ""

# Root Passwort
echo -e "${BOLD}${BLUE}[ Root Passwort ]${NC}"
while true; do
  read -s -p "$(echo -e "${YELLOW}Root Passwort:${NC} ")" ROOTPW
  echo ""
  read -s -p "$(echo -e "${YELLOW}Passwort bestätigen:${NC} ")" ROOTPW2
  echo ""
  if [ -z "$ROOTPW" ]; then
    echo -e "${RED}Passwort darf nicht leer sein.${NC}"
  elif [ "$ROOTPW" != "$ROOTPW2" ]; then
    echo -e "${RED}Passwörter stimmen nicht überein.${NC}"
  else
    echo -e "${GREEN}Passwort gesetzt.${NC}"
    break
  fi
done
echo ""

# Benutzer anlegen
echo -e "${BOLD}${BLUE}[ Benutzer ]${NC}"
read -p "$(echo -e "${YELLOW}Benutzername anlegen? (leer = nur root):${NC} ")" USERNAME

if [ -n "$USERNAME" ]; then
  while true; do
    read -s -p "$(echo -e "${YELLOW}Passwort für $USERNAME:${NC} ")" USERPW
    echo ""
    read -s -p "$(echo -e "${YELLOW}Passwort bestätigen:${NC} ")" USERPW2
    echo ""

    if [ "$USERPW" = "$USERPW2" ] && [ -n "$USERPW" ]; then
      echo -e "${GREEN}Benutzer $USERNAME wird angelegt.${NC}"
      break
    else
      echo -e "${RED}Passwörter stimmen nicht überein oder leer.${NC}"
    fi
  done

  # Benutzer im Zielsystem erstellen
  chroot /mnt/zenbyte useradd -m -s /bin/bash "$USERNAME" 2>/dev/null
  echo "$USERNAME:$USERPW" | chpasswd -R /mnt/zenbyte

  # sudo Rechte setzen
  echo "$USERNAME ALL=(ALL) ALL" > /mnt/zenbyte/etc/sudoers.d/$USERNAME
  chmod 440 /mnt/zenbyte/etc/sudoers.d/$USERNAME

  echo -e "${GREEN}Benutzer $USERNAME mit sudo-Rechten erstellt.${NC}"
fi
echo ""

# Zeitzone
echo -e "${BOLD}${BLUE}[ Zeitzone ]${NC}"
echo -e "  ${GREEN}1)${NC} Europe/Berlin"
echo -e "  ${GREEN}2)${NC} Europe/Vienna"
echo -e "  ${GREEN}3)${NC} Europe/Zurich"
echo -e "  ${GREEN}4)${NC} UTC"
echo -e "  ${GREEN}5)${NC} Andere eingeben"
read -p "$(echo -e "${YELLOW}Zeitzone [1]:${NC} ")" TZ_CHOICE
case "$TZ_CHOICE" in
  2) TIMEZONE="Europe/Vienna" ;;
  3) TIMEZONE="Europe/Zurich" ;;
  4) TIMEZONE="UTC" ;;
  5) read -p "Zeitzone eingeben: " TIMEZONE ;;
  *) TIMEZONE="Europe/Berlin" ;;
esac
echo -e "${GREEN}Zeitzone: $TIMEZONE${NC}"
echo ""

# Desktop Environment
echo -e "${BOLD}${BLUE}[ Desktop Environment ]${NC}"
echo -e "  ${GREEN}1)${NC} Nur Terminal (minimal)"
echo -e "  ${GREEN}2)${NC} XFCE4 Desktop (empfohlen)"
echo -e "  ${GREEN}3)${NC} Später selbst installieren"
read -p "$(echo -e "${YELLOW}Wahl [1]:${NC} ")" DE_CHOICE
echo ""

# Disk auswählen
echo -e "${BOLD}${BLUE}[ Festplatte ]${NC}"
echo -e "  Verfügbare Disks:"
lsblk -d -o NAME,SIZE,TYPE,MODEL 2>/dev/null | grep disk | while read line; do
  echo -e "  ${GREEN}•${NC} $line"
done
echo ""
read -p "$(echo -e "${YELLOW}Ziel-Disk (z.B. sda, vda, nvme0n1):${NC} ")" DISK
DISK="/dev/$DISK"

if [ ! -b "$DISK" ]; then
  echo -e "${RED}FEHLER: $DISK nicht gefunden.${NC}"
  exit 1
fi

# Partitionierung
echo ""
echo -e "${BOLD}${BLUE}[ Partitionierung ]${NC}"
echo -e "  ${GREEN}1)${NC} Gesamte Disk verwenden (einfach)"
echo -e "  ${GREEN}2)${NC} Manuelle Partitionierung (cfdisk)"
read -p "$(echo -e "${YELLOW}Wahl [1]:${NC} ")" PART_CHOICE
echo ""

# Zusammenfassung
echo -e "${BOLD}${BLUE}[ Zusammenfassung ]${NC}"
echo -e "  ${GREEN}•${NC} Hostname:  $HOSTNAME"
echo -e "  ${GREEN}•${NC} Zeitzone:  $TIMEZONE"
echo -e "  ${GREEN}•${NC} Disk:      $DISK"
echo -e "  ${GREEN}•${NC} Desktop:   $([ "$DE_CHOICE" = "2" ] && echo 'XFCE4' || echo 'Terminal')"
[ -n "$USERNAME" ] && echo -e "  ${GREEN}•${NC} Benutzer:  $USERNAME"
echo ""
echo -e "${RED}${BOLD}WARNUNG: Alle Daten auf $DISK werden gelöscht!${NC}"
read -p "$(echo -e "${YELLOW}Wirklich fortfahren? (ja/nein):${NC} ")" CONFIRM
if [ "$CONFIRM" != "ja" ]; then
  echo -e "${CYAN}Abgebrochen.${NC}"
  exit 0
fi

echo ""
echo -e "${BOLD}${BLUE}[ Installation ]${NC}"
echo ""

# Partitionierung
echo -e "${CYAN}[1/8]${NC} Partitioniere $DISK..."
if [ "$PART_CHOICE" = "2" ]; then
  cfdisk $DISK
  echo -e "Welche Partition verwenden?"
  read -p "Partition (z.B. sda1): " PART_NAME
  PART="/dev/$PART_NAME"
else
  parted -s $DISK mklabel msdos
  parted -s $DISK mkpart primary ext4 1MiB 100%
  parted -s $DISK set 1 boot on
  PART="${DISK}1"
  # nvme fix
  [[ "$DISK" == *"nvme"* ]] && PART="${DISK}p1"
fi
sleep 1

echo -e "${CYAN}[2/8]${NC} Formatiere $PART..."
mkfs.ext4 -F $PART -L ZenbyteOS

echo -e "${CYAN}[3/8]${NC} Mounte $PART..."
mkdir -p /mnt/zenbyte
mount $PART /mnt/zenbyte

echo -e "${CYAN}[4/8]${NC} Kopiere System..."
cp -ax / /mnt/zenbyte/ 2>/dev/null
echo -e "${GREEN}System kopiert.${NC}"

echo -e "${CYAN}[5/8]${NC} Konfiguriere System..."

# Hostname setzen
echo "$HOSTNAME" > /mnt/zenbyte/etc/hostname
cat > /mnt/zenbyte/etc/hosts << HOSTSEOF
127.0.0.1   localhost
127.0.1.1   $HOSTNAME
::1         localhost ip6-localhost ip6-loopback
HOSTSEOF

# Zeitzone
ln -sf /usr/share/zoneinfo/$TIMEZONE /mnt/zenbyte/etc/localtime 2>/dev/null

# Root Passwort
echo "root:$ROOTPW" | chpasswd -R /mnt/zenbyte

# Benutzer anlegen
if [ -n "$USERNAME" ]; then
  chroot /mnt/zenbyte useradd -m -s /bin/bash "$USERNAME" 2>/dev/null
  echo "$USERNAME:$USERPW" | chpasswd -R /mnt/zenbyte
  echo -e "${GREEN}Benutzer $USERNAME angelegt.${NC}"
fi

# Kernel kopieren
VMLINUZ=$(find /usr/lib/modules /boot -name "vmlinuz*" 2>/dev/null | head -1)
if [ -n "$VMLINUZ" ]; then
  cp "$VMLINUZ" /mnt/zenbyte/boot/vmlinuz
  echo -e "${GREEN}Kernel kopiert: $VMLINUZ${NC}"
else
  echo -e "${RED}WARNUNG: Kein Kernel gefunden!${NC}"
fi
cp /boot/initramfs.img /mnt/zenbyte/boot/initramfs.img 2>/dev/null || \
  find / -name "initramfs.img" 2>/dev/null | head -1 | xargs -I{} cp {} /mnt/zenbyte/boot/initramfs.img
echo -e "${CYAN}[7/8]${NC} Installiere GRUB..."
grub2-install --target=i386-pc \
  --boot-directory=/mnt/zenbyte/boot \
  --force $DISK 2>/dev/null

cat > /mnt/zenbyte/boot/grub2/grub.cfg << GRUBEOF
set timeout=5
set default=0

menuentry "ZenbyteOS 1.0" {
    linux /boot/vmlinuz root=$PART rw quiet selinux=0
    initrd /boot/initramfs.img
}

menuentry "ZenbyteOS 1.0 (Recovery)" {
    linux /boot/vmlinuz root=$PART rw selinux=0 init=/bin/bash
    initrd /boot/initramfs.img
}
GRUBEOF

echo -e "${CYAN}[8/8]${NC} Desktop einrichten..."
if [ "$DE_CHOICE" = "2" ]; then
  # Firstboot service für XFCE
  cat > /mnt/zenbyte/etc/systemd/system/zbpm-firstboot.service << 'SERVICE'
[Unit]
Description=ZenbyteOS First Boot - XFCE Setup
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
ExecStart=/bin/bash -c 'zbpm install xfce4-desktop && systemctl disable zbpm-firstboot'
RemainAfterExit=yes
StandardOutput=journal

[Install]
WantedBy=multi-user.target
SERVICE

  mkdir -p /mnt/zenbyte/etc/systemd/system/multi-user.target.wants
  ln -sf /etc/systemd/system/zbpm-firstboot.service \
    /mnt/zenbyte/etc/systemd/system/multi-user.target.wants/zbpm-firstboot.service
  echo -e "${GREEN}XFCE wird beim ersten Boot installiert.${NC}"
fi

umount /mnt/zenbyte

echo ""
echo -e "${BOLD}${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BOLD}${GREEN}  Installation erfolgreich abgeschlossen!${NC}"
echo -e "${BOLD}${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo -e "  ${CYAN}•${NC} Hostname:  ${BOLD}$HOSTNAME${NC}"
echo -e "  ${CYAN}•${NC} Disk:      ${BOLD}$PART${NC}"
echo -e "  ${CYAN}•${NC} Zeitzone:  ${BOLD}$TIMEZONE${NC}"
[ -n "$USERNAME" ] && echo -e "  ${CYAN}•${NC} Benutzer:  ${BOLD}$USERNAME${NC}"
echo ""
echo -e "  ${YELLOW}ISO/USB entfernen und neu starten!${NC}"
[ "$DE_CHOICE" = "2" ] && echo -e "  ${YELLOW}XFCE wird beim ersten Boot automatisch installiert.${NC}"
echo ""
