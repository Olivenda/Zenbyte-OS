#!/bin/bash


set -euo pipefail

# ─── Farben ────────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'


info()    { echo -e "  ${CYAN}[•]${NC} $*"; }
ok()      { echo -e "  ${GREEN}[✓]${NC} $*"; }
warn()    { echo -e "  ${YELLOW}[!]${NC} $*"; }
die()     { echo -e "\n  ${RED}[FEHLER]${NC} $*\n"; exit 1; }
step()    { echo -e "\n${BOLD}${BLUE}[ $* ]${NC}"; }
progress(){ echo -ne "  ${CYAN}[${1}/${TOTAL_STEPS}]${NC} $2..."; }
done_()   { echo -e " ${GREEN}fertig${NC}"; }

TOTAL_STEPS=9
TARGET=/mnt/zenbyte

[ "$(id -u)" -ne 0 ] && die "You need to run the installer as root"


clear
echo -e "${CYAN}${BOLD}"
cat << 'LOGO'
  ███████╗███████╗███╗   ██╗██████╗ ██╗   ██╗████████╗███████╗
  ╚════██║██╔════╝████╗  ██║══██╗╚██╗ ██╔╝╚══██╔══╝██╔════╝
      ██╔╝█████╗  ██╔██╗ ██║██████╔╝ ╚████╔╝    ██║   █████╗
     ██╔╝ ██╔══╝  ██║╚██╗██║██╔══██╗  ╚██╔╝     ██║   ██╔══╝
     ██║  ███████╗██║ ╚████║██████╔╝   ██║       ██║   ███████╗
     ╚═╝  ╚══════╝╚═╝  ╚═══╝╚═════╝    ╚═╝       ╚═╝   ╚══════╝
LOGO
echo -e "${NC}"
echo -e "  ${CYAN}ZenbyteOS Installer v4.0${NC}"
echo -e "  ${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""


step "System Information"
echo -e "  ${GREEN}•${NC} CPU:    $(grep 'model name' /proc/cpuinfo 2>/dev/null | head -1 | cut -d: -f2 | xargs || echo 'Unknown')"
echo -e "  ${GREEN}•${NC} RAM:    $(free -h 2>/dev/null | awk '/^Mem:/{print $2}' || echo 'Unkown')"
echo -e "  ${GREEN}•${NC} Kernel: $(uname -r)"
echo ""

read -rp "$(echo -e "  ${YELLOW}Start Installation? [j/N]:${NC} ")" START
[[ "$START" != "j" && "$START" != "J" ]] && echo -e "\n  ${CYAN}Cancelled.${NC}\n" && exit 0


step "Hostname"
read -rp "$(echo -e "  ${YELLOW}Hostname [zenbyte]:${NC} ")" HOSTNAME
HOSTNAME="${HOSTNAME:-zenbyte}"

if ! echo "$HOSTNAME" | grep -qE '^[a-zA-Z0-9][a-zA-Z0-9\-]{0,62}$'; then
  die "Invalid Hostname: '$HOSTNAME'"
fi
ok "Hostname: $HOSTNAME"

step "Root Password"
while true; do
  read -rsp "$(echo -e "  ${YELLOW}Root Password:${NC} ")" ROOTPW; echo ""
  read -rsp "$(echo -e "  ${YELLOW}Confirm:${NC} ")"   ROOTPW2; echo ""
  [ -z "$ROOTPW" ]          && warn "The password cannot be empty." && continue
  [ ${#ROOTPW} -lt 6 ]      && warn "The password must have 6 or more characters." && continue
  [ "$ROOTPW" != "$ROOTPW2" ] && warn "Passwords do not match." && continue
  ok "Root Password set."; break
done


step "Create a User"
USERNAME=""
USERPW=""
USER_ROLE=1

read -rp "$(echo -e "  ${YELLOW}Username (empty = only root):${NC} ")" USERNAME

if [ -n "$USERNAME" ]; then

  if ! echo "$USERNAME" | grep -qE '^[a-z_][a-z0-9_\-]{0,30}$'; then
    die "Invalid Username: '$USERNAME' (only lower Case Letters, Numbers, _ and -)"
  fi

  while true; do
    read -rsp "$(echo -e "  ${YELLOW}Password for $USERNAME:${NC} ")" USERPW;  echo ""
    read -rsp "$(echo -e "  ${YELLOW}Confirm:${NC} ")"               USERPW2; echo ""
    [ -z "$USERPW" ]            && warn "The password cannot be empty." && continue
    [ ${#USERPW} -lt 6 ]      && warn "The password must have 6 or more characters." && continue
    [ "$USERPW" != "$USERPW2" ] && warn "Passwords do not match." && continue
    break
  done

  echo ""
  step "User Permissions"
  echo -e "  ${GREEN}1)${NC} Standard User"
  echo -e "  ${GREEN}2)${NC} Administrator (sudo/wheel) ${BOLD}[Standard]${NC}"
  read -rp "$(echo -e "  ${YELLOW}Choice [2]:${NC} ")" USER_ROLE_INPUT
  USER_ROLE="${USER_ROLE_INPUT:-2}"
  ok "Creating user '$USERNAME'."
fi


step "Timezone"
echo -e "  ${GREEN}1)${NC} Europe/Berlin  ${BOLD}[Default]${NC}"
echo -e "  ${GREEN}2)${NC} Europe/Vienna"
echo -e "  ${GREEN}3)${NC} Europe/Zurich"
echo -e "  ${GREEN}4)${NC} UTC"
echo -e "  ${GREEN}5)${NC} Enter another:"
read -rp "$(echo -e "  ${YELLOW}Choice [1]:${NC} ")" TZ_CHOICE
case "${TZ_CHOICE:-1}" in
  2) TIMEZONE="Europe/Vienna"  ;;
  3) TIMEZONE="Europe/Zurich"  ;;
  4) TIMEZONE="UTC"            ;;
  5) read -rp "  Timezone: " TIMEZONE ;;
  *) TIMEZONE="Europe/Berlin"  ;;
esac

[ ! -f "/usr/share/zoneinfo/$TIMEZONE" ] && die "Ungültige Zeitzone: $TIMEZONE"
ok "Timezone: $TIMEZONE"


step "Desktop Environment"
echo -e "  ${GREEN}1)${NC} Terminal only (minimal installation)"
echo -e "  ${GREEN}2)${NC} XFCE4 Desktop (via zbpm on first boot) ${BOLD}[Standard]${NC}"
read -rp "$(echo -e "  ${YELLOW}Choice [2]:${NC} ")" DE_CHOICE
DE_CHOICE="${DE_CHOICE:-2}"


step "Boot-Mode"
echo -e "  ${GREEN}1)${NC} BIOS/Legacy (MBR + GRUB)  ${BOLD}[Standard]${NC}"
echo -e "  ${GREEN}2)${NC} UEFI (GPT + GRUB-EFI)"
read -rp "$(echo -e "  ${YELLOW}Choice [1]:${NC} ")" BOOT_MODE
BOOT_MODE="${BOOT_MODE:-1}"


step "Harddisk"
echo -e "  Available Disks:\n"
lsblk -d -o NAME,SIZE,TYPE,MODEL 2>/dev/null | grep disk | while IFS= read -r line; do
  echo -e "  ${GREEN}•${NC} $line"
done
echo ""

echo -e "  ${GREEN}1)${NC} Partition whole Disk automatically ${BOLD}[Standard]${NC}"
echo -e "  ${GREEN}2)${NC} Partition manual with cfdisk"
read -rp "$(echo -e "  ${YELLOW}Partition method [1]:${NC} ")" PART_METHOD
PART_METHOD="${PART_METHOD:-1}"
echo ""

while true; do
  read -rp "$(echo -e "  ${YELLOW}Target-Disk (z.B. sda, vda, nvme0n1):${NC} ")" DISK_NAME
  DISK="/dev/$DISK_NAME"
  [ -b "$DISK" ] && break
  warn "$DISK not found. Try again."
done


clear
echo -e "${BOLD}${BLUE}[ Summary ]${NC}"
echo -e "  ${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "  ${GREEN}•${NC} Hostname:   ${BOLD}$HOSTNAME${NC}"
echo -e "  ${GREEN}•${NC} Timezone:   ${BOLD}$TIMEZONE${NC}"
echo -e "  ${GREEN}•${NC} Disk:       ${BOLD}$DISK${NC}"
echo -e "  ${GREEN}•${NC} Boot-Mode: ${BOLD}$([ "$BOOT_MODE" = "2" ] && echo 'UEFI' || echo 'BIOS/Legacy')${NC}"
echo -e "  ${GREEN}•${NC} Desktop:    ${BOLD}$([ "$DE_CHOICE" = "2" ] && echo 'XFCE4' || echo 'Terminal')${NC}"
[ -n "$USERNAME" ] && echo -e "  ${GREEN}•${NC} User:   ${BOLD}$USERNAME$([ "$USER_ROLE" = "2" ] && echo ' (Admin)' || echo '')${NC}"
echo ""
echo -e "  ${RED}${BOLD}WARNING: EVERYTHING on $DISK will be deleted${NC}"
echo ""
read -rp "$(echo -e "  ${YELLOW}Continue? Enter 'yes, do as I say daddy':${NC} ")" CONFIRM
[ "$CONFIRM" != "yes, do as I say daddy" ] && echo -e "\n  ${CYAN}Cancelled.${NC}\n" && exit 0
echo "Good boy!"
sleep 3


echo ""
step "Installation in progress"
echo ""


progress 1 "Partitioning $DISK"


umount "${DISK}"* 2>/dev/null || true
swapoff "${DISK}"* 2>/dev/null || true

if [ "$PART_METHOD" = "2" ]; then

  echo ""
  warn "cfdisk is opening. Create Partition, make bootable, save with 'Write'."
  sleep 2
  cfdisk "$DISK"
  echo ""
  lsblk "$DISK" -o NAME,SIZE,TYPE | grep part
  echo ""
  read -rp "$(echo -e "  ${YELLOW}Which Partition to use? (z.B. sda1):${NC} ")" PART_NAME
  PART="/dev/$PART_NAME"
  [ ! -b "$PART" ] && die "$PART not found."
  read -rp "$(echo -e "  ${YELLOW}Format Partition? [j/N]:${NC} ")" FORMAT_PART
  if [[ "${FORMAT_PART:-n}" == "j" || "${FORMAT_PART:-n}" == "J" ]]; then
    mkfs.ext4 -F "$PART" -L ZenbyteOS > /dev/null
  fi
  BOOT_PART="$PART"
  ROOT_PART="$PART"
else

  if [ "$BOOT_MODE" = "2" ]; then

    parted -s "$DISK" mklabel gpt
    parted -s "$DISK" mkpart ESP fat32 1MiB 513MiB
    parted -s "$DISK" set 1 esp on
    parted -s "$DISK" mkpart primary ext4 513MiB 100%
    if [[ "$DISK" == *"nvme"* ]]; then
      BOOT_PART="${DISK}p1"
      ROOT_PART="${DISK}p2"
    else
      BOOT_PART="${DISK}1"
      ROOT_PART="${DISK}2"
    fi
    mkfs.fat -F32 "$BOOT_PART" > /dev/null
    mkfs.ext4 -F "$ROOT_PART" -L ZenbyteOS > /dev/null
  else

    parted -s "$DISK" mklabel msdos
    parted -s "$DISK" mkpart primary ext4 1MiB 100%
    parted -s "$DISK" set 1 boot on
    if [[ "$DISK" == *"nvme"* ]]; then
      ROOT_PART="${DISK}p1"
    else
      ROOT_PART="${DISK}1"
    fi
    BOOT_PART="$ROOT_PART"
    sleep 1
    mkfs.ext4 -F "$ROOT_PART" -L ZenbyteOS > /dev/null
  fi
fi
done_


progress 2 "Mounting Partitions"
mkdir -p "$TARGET"
mount "$ROOT_PART" "$TARGET"
if [ "$BOOT_MODE" = "2" ] && [ "$BOOT_PART" != "$ROOT_PART" ]; then
  mkdir -p "$TARGET/boot/efi"
  mount "$BOOT_PART" "$TARGET/boot/efi"
fi
done_


progress 3 "Copying System (Can take a few minutes)"

rsync -aAX \
  --exclude=/proc \
  --exclude=/sys \
  --exclude=/dev \
  --exclude=/run \
  --exclude=/tmp \
  --exclude=/mnt \
  --exclude=/media \
  --exclude=/lost+found \
  --exclude=/install.sh \
  / "$TARGET/" 2>/dev/null || \
cp -ax \
  --exclude=/proc \
  --exclude=/sys \
  --exclude=/dev \
  --exclude=/run \
  --exclude=/tmp \
  --exclude=/mnt \
  --exclude=/media \
  / "$TARGET/" 2>/dev/null || true


mkdir -p "$TARGET"/{proc,sys,dev,run,tmp,mnt,media}
chmod 1777 "$TARGET/tmp"
done_

progress 4 "Configuring System"

# Hostname
echo "$HOSTNAME" > "$TARGET/etc/hostname"
cat > "$TARGET/etc/hosts" << HOSTSEOF
127.0.0.1   localhost
127.0.1.1   $HOSTNAME
::1         localhost ip6-localhost ip6-loopback
ff02::1     ip6-allnodes
ff02::2     ip6-allrouters
HOSTSEOF

# Zeitzone
ln -sf "/usr/share/zoneinfo/$TIMEZONE" "$TARGET/etc/localtime"
echo "$TIMEZONE" > "$TARGET/etc/timezone"

# Locale
cat > "$TARGET/etc/locale.conf" << 'LOCEOF'
LANG=de_DE.UTF-8
LC_TIME=de_DE.UTF-8
LOCEOF

# os-release (ZenbyteOS Branding)
cat > "$TARGET/etc/os-release" << OSEOF
NAME="ZenbyteOS"
ID=zenbyteos
VERSION="1.0"
VERSION_ID="1.0"
PRETTY_NAME="ZenbyteOS 1.0"
HOME_URL="https://zbpm.cronodevelopment.com"
SUPPORT_URL="https://zbpm.cronodevelopment.com"
OSEOF
cp "$TARGET/etc/os-release" "$TARGET/usr/lib/os-release" 2>/dev/null || true

# fstab
ROOT_UUID=$(blkid -s UUID -o value "$ROOT_PART")
echo "# ZenbyteOS fstab" > "$TARGET/etc/fstab"
echo "UUID=$ROOT_UUID  /       ext4  defaults  1 1" >> "$TARGET/etc/fstab"
if [ "$BOOT_MODE" = "2" ] && [ "$BOOT_PART" != "$ROOT_PART" ]; then
  EFI_UUID=$(blkid -s UUID -o value "$BOOT_PART")
  echo "UUID=$EFI_UUID   /boot/efi  vfat  umask=0077  0 2" >> "$TARGET/etc/fstab"
fi
echo "tmpfs  /tmp  tmpfs  defaults,nosuid,nodev  0 0" >> "$TARGET/etc/fstab"
done_


progress 5 "Setting up Users and Passwords"

# Root Passwort setzen
echo "root:$ROOTPW" | chpasswd -R "$TARGET"

# Sudoers sauber schreiben
cat > "$TARGET/etc/sudoers" << 'SUDOEOF'
Defaults env_reset
Defaults mail_badpass
Defaults secure_path="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
Defaults !visiblepw

root    ALL=(ALL:ALL) ALL
%wheel  ALL=(ALL:ALL) ALL
%sudo   ALL=(ALL:ALL) ALL

@includedir /etc/sudoers.d
SUDOEOF
chown root:root "$TARGET/etc/sudoers"
chmod 440 "$TARGET/etc/sudoers"
mkdir -p "$TARGET/etc/sudoers.d"
chmod 750 "$TARGET/etc/sudoers.d"


if [ -n "$USERNAME" ]; then
  chroot "$TARGET" useradd -m -s /bin/bash -c "$USERNAME" "$USERNAME" 2>/dev/null || \
  chroot "$TARGET" usermod -s /bin/bash "$USERNAME" 2>/dev/null || true
  echo "$USERNAME:$USERPW" | chpasswd -R "$TARGET"

  if [ "$USER_ROLE" = "2" ]; then
    chroot "$TARGET" groupadd -f wheel 2>/dev/null || true
    chroot "$TARGET" groupadd -f sudo  2>/dev/null || true
    chroot "$TARGET" usermod -aG wheel,sudo "$USERNAME" 2>/dev/null || true
  fi
fi
done_


progress 6 "Securing System Permissions"

# Kritische Dateien
chown root:root   "$TARGET/etc/passwd"     && chmod 644  "$TARGET/etc/passwd"
chown root:shadow "$TARGET/etc/shadow"  2>/dev/null && chmod 640  "$TARGET/etc/shadow"  || \
  chown root:root "$TARGET/etc/shadow"    && chmod 640  "$TARGET/etc/shadow"
chown root:root   "$TARGET/etc/group"      && chmod 644  "$TARGET/etc/group"
chown root:shadow "$TARGET/etc/gshadow" 2>/dev/null && chmod 640  "$TARGET/etc/gshadow" || \
  chown root:root "$TARGET/etc/gshadow"   && chmod 640  "$TARGET/etc/gshadow"

# sudo binary
[ -f "$TARGET/usr/bin/sudo" ] && chown root:root "$TARGET/usr/bin/sudo" && chmod 4755 "$TARGET/usr/bin/sudo"
[ -f "$TARGET/usr/bin/su"   ] && chown root:root "$TARGET/usr/bin/su"   && chmod 4755 "$TARGET/usr/bin/su"


rm -f "$TARGET/etc/ssh/ssh_host_"* 2>/dev/null || true


echo "" > "$TARGET/etc/machine-id" 2>/dev/null || true
done_


progress 7 "Kernel & Initramfs"
mkdir -p "$TARGET/boot"

VMLINUZ=$(find /boot /usr/lib/modules -maxdepth 3 -name "vmlinuz*" 2>/dev/null \
  | grep -v rescue | sort -V | tail -1)
INITRAMFS=$(find /boot -maxdepth 2 -name "initramfs*.img" 2>/dev/null \
  | grep -v rescue | grep -v kdump | sort -V | tail -1)

if [ -n "$VMLINUZ" ]; then
  cp "$VMLINUZ"  "$TARGET/boot/vmlinuz"
else
  warn "No Kernel has been found - Boot could fail"
fi

if [ -n "$INITRAMFS" ]; then
  cp "$INITRAMFS" "$TARGET/boot/initramfs.img"
else
  warn "No initramfs has been found — Boot could fail"
fi
done_


progress 8 "Bootloader (GRUB)"


mount --bind /proc "$TARGET/proc"
mount --bind /sys  "$TARGET/sys"
mount --bind /dev  "$TARGET/dev"

if [ "$BOOT_MODE" = "2" ]; then

  mount --bind /sys/firmware/efi/efivars "$TARGET/sys/firmware/efi/efivars" 2>/dev/null || true
  chroot "$TARGET" grub2-install --target=x86_64-efi \
    --efi-directory=/boot/efi \
    --bootloader-id=ZenbyteOS \
    --recheck 2>/dev/null || \
  chroot "$TARGET" grub-install --target=x86_64-efi \
    --efi-directory=/boot/efi \
    --bootloader-id=ZenbyteOS \
    --recheck 2>/dev/null || \
    warn "GRUB-EFI could not be installed."
else

  chroot "$TARGET" grub2-install --target=i386-pc \
    --boot-directory=/boot --recheck "$DISK" 2>/dev/null || \
  chroot "$TARGET" grub-install --target=i386-pc \
    --boot-directory=/boot --recheck "$DISK" 2>/dev/null || \
    warn "GRUB-BIOS could not be installed."
fi


GRUB_CFG_DIR="$TARGET/boot/grub2"
[ ! -d "$GRUB_CFG_DIR" ] && GRUB_CFG_DIR="$TARGET/boot/grub"
mkdir -p "$GRUB_CFG_DIR"

cat > "$GRUB_CFG_DIR/grub.cfg" << GRUBEOF
set timeout=5
set default=0
set timeout_style=menu

if loadfont /boot/grub2/fonts/unicode.pf2; then
  set gfxmode=auto
  insmod all_video
  terminal_output gfxterm
fi

menuentry "ZenbyteOS 1.0" {
    linux  /boot/vmlinuz root=UUID=$ROOT_UUID rw quiet selinux=0 loglevel=3
    initrd /boot/initramfs.img
}

menuentry "ZenbyteOS 1.0 — Recovery" {
    linux  /boot/vmlinuz root=UUID=$ROOT_UUID rw selinux=0 loglevel=7 init=/bin/bash
    initrd /boot/initramfs.img
}
GRUBEOF

# Bind-Mounts lösen
umount "$TARGET/proc" "$TARGET/sys" "$TARGET/dev" 2>/dev/null || true
umount "$TARGET/sys/firmware/efi/efivars" 2>/dev/null || true
done_


progress 9 "Setting up First-Boot services"

if [ "$DE_CHOICE" = "2" ]; then
  mkdir -p "$TARGET/etc/systemd/system"
  cat > "$TARGET/etc/systemd/system/zbpm-firstboot.service" << 'SERVICE'
[Unit]
Description=ZenbyteOS First Boot — Desktop einrichten
After=network-online.target
Wants=network-online.target
ConditionPathExists=/etc/zbpm-firstboot-pending

[Service]
Type=oneshot
ExecStart=/bin/bash -c '/usr/bin/zbpm install zenbyte-desktop && rm -f /etc/zbpm-firstboot-pending'
RemainAfterExit=yes
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
SERVICE

  # Marker-Datei damit der Service nur einmal läuft
  touch "$TARGET/etc/zbpm-firstboot-pending"

  mkdir -p "$TARGET/etc/systemd/system/multi-user.target.wants"
  ln -sf /etc/systemd/system/zbpm-firstboot.service \
    "$TARGET/etc/systemd/system/multi-user.target.wants/zbpm-firstboot.service"
fi
done_

# ─── Alles aushängen ──────────────────────────────────────────────────────────
sync
umount "$TARGET/boot/efi" 2>/dev/null || true
umount "$TARGET" 2>/dev/null || true

echo ""
echo -e "  ${BOLD}${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "  ${BOLD}${GREEN}  Installation completed successfuly!${NC}"
echo -e "  ${BOLD}${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo -e "  ${CYAN}•${NC} Hostname:    ${BOLD}$HOSTNAME${NC}"
echo -e "  ${CYAN}•${NC} Disc:        ${BOLD}$ROOT_PART${NC}"
echo -e "  ${CYAN}•${NC} Timezone:    ${BOLD}$TIMEZONE${NC}"
echo -e "  ${CYAN}•${NC} Boot-Mode:  ${BOLD}$([ "$BOOT_MODE" = "2" ] && echo 'UEFI' || echo 'BIOS/Legacy')${NC}"
[ -n "$USERNAME" ] && echo -e "  ${CYAN}•${NC} User:    ${BOLD}$USERNAME$([ "$USER_ROLE" = "2" ] && echo ' (Admin)' || echo '')${NC}"
[ "$DE_CHOICE" = "2" ] && \
  echo -e "  ${CYAN}•${NC} Desktop:     ${BOLD}XFCE4 (will be installed on first Boot)${NC}"
echo ""
echo -e "  ${YELLOW}Remove ISO/USB and restart device${NC}"
echo ""██╔
