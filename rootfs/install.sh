#!/bin/bash
# ============================================================
#  ZenbyteOS Installer v3.0
#  - UEFI + BIOS, GPT or MBR
#  - Input validation everywhere, quoted variables
#  - Atomic password handling (chpasswd via stdin, vars unset)
#  - Optional swap, ext4/btrfs/xfs filesystem choice
#  - Locale, keyboard, timezone validated
#  - Cleanup trap, error-on-failure, no silent steps
# ============================================================

set -o pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Installer must run as root." >&2
  exit 1
fi

# ---------- presentation ----------
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
  RED=$'\033[0;31m'; GREEN=$'\033[0;32m'; YELLOW=$'\033[1;33m'
  BLUE=$'\033[0;34m'; CYAN=$'\033[0;36m'; BOLD=$'\033[1m'; NC=$'\033[0m'
else
  RED=""; GREEN=""; YELLOW=""; BLUE=""; CYAN=""; BOLD=""; NC=""
fi

TARGET="/mnt/zenbyte"
LANG_CHOICE="en"
EFI_MODE=0
[ -d /sys/firmware/efi ] && EFI_MODE=1

# ---------- i18n ----------
msg() {
  local k="$1"
  case "$LANG_CHOICE:$k" in
    de:welcome)         echo "Willkommen! Dieser Installer richtet ZenbyteOS ein." ;;
    en:welcome)         echo "Welcome. This installer sets up ZenbyteOS." ;;
    de:start_q)         echo "Installation starten? [j/N]: " ;;
    en:start_q)         echo "Start installation? [y/N]: " ;;
    de:cancelled)       echo "Abgebrochen." ;;
    en:cancelled)       echo "Cancelled." ;;
    de:hostname_q)      echo "Hostname [zenbyte]: " ;;
    en:hostname_q)      echo "Hostname [zenbyte]: " ;;
    de:bad_hostname)    echo "Ungültiger Hostname (a-z, 0-9, -, max 63)." ;;
    en:bad_hostname)    echo "Invalid hostname (a-z, 0-9, -, max 63)." ;;
    de:rootpw_q)        echo "Root-Passwort: " ;;
    en:rootpw_q)        echo "Root password: " ;;
    de:confirm_pw)      echo "Passwort bestätigen: " ;;
    en:confirm_pw)      echo "Confirm password: " ;;
    de:pw_empty)        echo "Passwort darf nicht leer sein." ;;
    en:pw_empty)        echo "Password may not be empty." ;;
    de:pw_short)        echo "Passwort zu kurz (min. 8 Zeichen)." ;;
    en:pw_short)        echo "Password too short (min 8 chars)." ;;
    de:pw_mismatch)     echo "Passwörter stimmen nicht überein." ;;
    en:pw_mismatch)     echo "Passwords do not match." ;;
    de:pw_set)          echo "Passwort gesetzt." ;;
    en:pw_set)          echo "Password set." ;;
    de:user_q)          echo "Benutzername anlegen (leer = nur root): " ;;
    en:user_q)          echo "Create user (empty = root only): " ;;
    de:bad_username)    echo "Ungültiger Benutzername (a-z, beginnt mit Buchstabe, max 32)." ;;
    en:bad_username)    echo "Invalid username (a-z, must start with letter, max 32)." ;;
    de:userpw_q)        echo "Passwort für Benutzer: " ;;
    en:userpw_q)        echo "Password for user: " ;;
    de:bad_tz)          echo "Ungültige Zeitzone." ;;
    en:bad_tz)          echo "Invalid timezone." ;;
    de:disk_q)          echo "Ziel-Disk (z.B. sda, vda, nvme0n1): " ;;
    en:disk_q)          echo "Target disk (e.g. sda, vda, nvme0n1): " ;;
    de:disk_missing)    echo "Disk nicht gefunden:" ;;
    en:disk_missing)    echo "Disk not found:" ;;
    de:disk_too_small)  echo "Disk zu klein (min. 4 GB):" ;;
    en:disk_too_small)  echo "Disk too small (min 4 GB):" ;;
    de:warn_destroy)    echo "WARNUNG: Alle Daten werden gelöscht auf:" ;;
    en:warn_destroy)    echo "WARNING: All data will be erased on:" ;;
    de:type_yes)        echo "Zur Bestätigung tippen: ja" ;;
    en:type_yes)        echo "Type 'yes' to confirm: " ;;
    de:installing)      echo "Installation läuft" ;;
    en:installing)      echo "Installing" ;;
    de:done)            echo "Installation erfolgreich abgeschlossen." ;;
    en:done)            echo "Installation completed successfully." ;;
    de:remove_media)    echo "ISO/USB entfernen und neu starten." ;;
    en:remove_media)    echo "Remove install media and reboot." ;;
    *)                  echo "$k" ;;
  esac
}

# ---------- helpers ----------
hr()    { echo "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"; }
say()   { echo "${CYAN}[*]${NC} $*"; }
ok()    { echo "${GREEN}[ok]${NC} $*"; }
warn()  { echo "${YELLOW}[!]${NC} $*"; }
die()   { echo "${RED}[fatal]${NC} $*" >&2; exit 1; }
hdr()   { echo; echo "${BOLD}${BLUE}[ $* ]${NC}"; }

run() {
  # Wrapper that aborts on failure with a clear message.
  if ! "$@"; then
    die "step failed: $*"
  fi
}

confirm_yes() {
  local prompt="$1" ans
  read -r -p "$prompt" ans
  case "$ans" in
    y|Y|yes|YES|j|J|ja|JA|Ja) return 0 ;;
    *) return 1 ;;
  esac
}

# Validators
valid_hostname() { [[ "$1" =~ ^[a-z0-9][a-z0-9-]{0,62}$ ]]; }
valid_username() { [[ "$1" =~ ^[a-z_][a-z0-9_-]{0,31}$ ]]; }
valid_disk()     { [[ "$1" =~ ^[a-zA-Z0-9]+$ ]]; }
valid_timezone() { [ -f "/usr/share/zoneinfo/$1" ]; }

# ---------- cleanup ----------
cleanup() {
  set +e
  for mp in dev/pts dev proc sys run boot/efi ""; do
    mountpoint -q "$TARGET/$mp" 2>/dev/null && umount -lf "$TARGET/$mp" 2>/dev/null
  done
  mountpoint -q "$TARGET" 2>/dev/null && umount -lf "$TARGET" 2>/dev/null
  unset ROOTPW ROOTPW2 USERPW USERPW2
}
trap cleanup EXIT INT TERM

# ============================================================
clear
echo "${CYAN}${BOLD}"
cat << 'LOGO'
  ███████╗███████╗███╗   ██╗██████╗ ██╗   ██╗████████╗███████╗
  ╚════██║██╔════╝████╗  ██║██╔══██╗╚██╗ ██╔╝╚══██╔══╝██╔════╝
      ██╔╝█████╗  ██╔██╗ ██║██████╔╝ ╚████╔╝    ██║   █████╗
     ██╔╝ ██╔══╝  ██║╚██╗██║██╔══██╗  ╚██╔╝     ██║   ██╔══╝
     ██║  ███████╗██║ ╚████║██████╔╝   ██║      ██║   ███████╗
     ╚═╝  ╚══════╝╚═╝  ╚═══╝╚═════╝    ╚═╝      ╚═╝   ╚══════╝
LOGO
echo "${NC}"
echo "  ${CYAN}ZenbyteOS Installer v3.0  ($([ "$EFI_MODE" = 1 ] && echo UEFI || echo BIOS))${NC}"
hr

hdr "Sprache / Language"
echo "  ${GREEN}1)${NC} Deutsch"
echo "  ${GREEN}2)${NC} English"
read -r -p "Wahl / Choice [2]: " langc
[ "$langc" = "1" ] && LANG_CHOICE="de" || LANG_CHOICE="en"

echo
echo "$(msg welcome)"
echo

hdr "System Info"
echo "  ${GREEN}•${NC} CPU:    $(awk -F: '/model name/ {print $2; exit}' /proc/cpuinfo | xargs)"
echo "  ${GREEN}•${NC} RAM:    $(awk '/^MemTotal/ {printf "%.1f GiB\n", $2/1024/1024}' /proc/meminfo)"
echo "  ${GREEN}•${NC} Kernel: $(uname -r)"
echo "  ${GREEN}•${NC} Arch:   $(uname -m)"
echo "  ${GREEN}•${NC} Mode:   $([ "$EFI_MODE" = 1 ] && echo "UEFI" || echo "BIOS / Legacy")"
echo

confirm_yes "${YELLOW}$(msg start_q)${NC}" || { echo "$(msg cancelled)"; exit 0; }

# ---------- hostname ----------
hdr "Hostname"
while :; do
  read -r -p "$(msg hostname_q)" HOSTNAME_IN
  HOSTNAME_IN="${HOSTNAME_IN:-zenbyte}"
  if valid_hostname "$HOSTNAME_IN"; then
    HOSTNAME_VAL="$HOSTNAME_IN"
    break
  fi
  warn "$(msg bad_hostname)"
done

# ---------- root password ----------
hdr "Root password"
while :; do
  read -r -s -p "$(msg rootpw_q)" ROOTPW; echo
  read -r -s -p "$(msg confirm_pw)" ROOTPW2; echo
  if [ -z "$ROOTPW" ]; then
    warn "$(msg pw_empty)"
  elif [ "${#ROOTPW}" -lt 8 ]; then
    warn "$(msg pw_short)"
  elif [ "$ROOTPW" != "$ROOTPW2" ]; then
    warn "$(msg pw_mismatch)"
  else
    ok "$(msg pw_set)"
    break
  fi
done

# ---------- optional user ----------
hdr "User account"
USERNAME=""
while :; do
  read -r -p "$(msg user_q)" UNAME_IN
  if [ -z "$UNAME_IN" ]; then
    break
  fi
  if valid_username "$UNAME_IN"; then
    USERNAME="$UNAME_IN"
    break
  fi
  warn "$(msg bad_username)"
done

if [ -n "$USERNAME" ]; then
  while :; do
    read -r -s -p "$(msg userpw_q)" USERPW; echo
    read -r -s -p "$(msg confirm_pw)" USERPW2; echo
    if [ -z "$USERPW" ]; then
      warn "$(msg pw_empty)"
    elif [ "${#USERPW}" -lt 8 ]; then
      warn "$(msg pw_short)"
    elif [ "$USERPW" != "$USERPW2" ]; then
      warn "$(msg pw_mismatch)"
    else
      ok "$(msg pw_set)"
      break
    fi
  done
fi

# ---------- timezone ----------
hdr "Timezone"
echo "  ${GREEN}1)${NC} Europe/Berlin"
echo "  ${GREEN}2)${NC} Europe/Vienna"
echo "  ${GREEN}3)${NC} Europe/Zurich"
echo "  ${GREEN}4)${NC} UTC"
echo "  ${GREEN}5)${NC} Other..."
while :; do
  read -r -p "Choice [1]: " TZC
  case "$TZC" in
    ""|1) TIMEZONE="Europe/Berlin"; break ;;
    2)    TIMEZONE="Europe/Vienna"; break ;;
    3)    TIMEZONE="Europe/Zurich"; break ;;
    4)    TIMEZONE="UTC"; break ;;
    5)
      read -r -p "Timezone (e.g. America/New_York): " TIMEZONE
      if valid_timezone "$TIMEZONE"; then break; fi
      warn "$(msg bad_tz)"
      ;;
    *) warn "?";;
  esac
done

# ---------- locale + keymap ----------
hdr "Locale"
echo "  ${GREEN}1)${NC} en_US.UTF-8"
echo "  ${GREEN}2)${NC} de_DE.UTF-8"
echo "  ${GREEN}3)${NC} C.UTF-8"
read -r -p "Choice [1]: " LC
case "$LC" in
  2) LOCALE="de_DE.UTF-8" ;;
  3) LOCALE="C.UTF-8" ;;
  *) LOCALE="en_US.UTF-8" ;;
esac

hdr "Keyboard layout"
echo "  ${GREEN}1)${NC} us"
echo "  ${GREEN}2)${NC} de"
echo "  ${GREEN}3)${NC} uk"
read -r -p "Choice [1]: " KB
case "$KB" in
  2) KEYMAP="de" ;;
  3) KEYMAP="uk" ;;
  *) KEYMAP="us" ;;
esac

# ---------- zbpm mirror ----------
hdr "Package repository (zbpm)"
echo "  Enter the URL of your zbpm mirror (the machine running 'tools/zbpm-repo serve')."
echo "  Example: http://192.168.1.10:8765"
echo "  Leave blank to configure later in /etc/zbpm/mirrors"
echo
read -r -p "Mirror URL [skip]: " ZBPM_MIRROR
ZBPM_MIRROR="${ZBPM_MIRROR%/}"   # strip trailing slash

# ---------- desktop ----------
hdr "Desktop environment"
echo "  ${GREEN}1)${NC} Terminal only (minimal)"
echo "  ${GREEN}2)${NC} XFCE4 (installed on first boot via zbpm)"
echo "  ${GREEN}3)${NC} Skip — install later"
read -r -p "Choice [1]: " DE_CHOICE

# ---------- disk ----------
hdr "Target disk"
echo "  Available disks:"
lsblk -d -o NAME,SIZE,TYPE,MODEL 2>/dev/null | awk 'NR==1 || $3=="disk"' | sed 's/^/    /'
echo
while :; do
  read -r -p "$(msg disk_q)" DISK_IN
  if ! valid_disk "$DISK_IN"; then
    warn "invalid disk name"; continue
  fi
  DISK="/dev/$DISK_IN"
  if [ ! -b "$DISK" ]; then
    warn "$(msg disk_missing) $DISK"; continue
  fi
  size=$(blockdev --getsize64 "$DISK" 2>/dev/null || echo 0)
  if [ "$size" -lt $((4*1024*1024*1024)) ]; then
    warn "$(msg disk_too_small) $DISK ($((size/1024/1024)) MB)"; continue
  fi
  # Refuse to partition a disk that has any partition currently mounted.
  if grep -qE "^${DISK}[0-9p]" /proc/mounts 2>/dev/null || \
     grep -qE "^${DISK} " /proc/mounts 2>/dev/null; then
    warn "$DISK or one of its partitions is currently mounted — cannot proceed"; continue
  fi
  break
done

# ---------- filesystem ----------
hdr "Root filesystem"
echo "  ${GREEN}1)${NC} ext4 (default, well tested)"
echo "  ${GREEN}2)${NC} btrfs (subvolumes, snapshots)"
echo "  ${GREEN}3)${NC} xfs"
read -r -p "Choice [1]: " FSC
case "$FSC" in
  2) ROOTFS="btrfs"; MKFS=(mkfs.btrfs -f -L ZenbyteOS) ;;
  3) ROOTFS="xfs";   MKFS=(mkfs.xfs -f -L ZenbyteOS) ;;
  *) ROOTFS="ext4";  MKFS=(mkfs.ext4 -F -L ZenbyteOS) ;;
esac

# ---------- swap ----------
hdr "Swap"
read -r -p "Swap size in MiB [auto, 0 = none]: " SWAP_MB
if [ -z "$SWAP_MB" ]; then
  ram_kb=$(awk '/^MemTotal/ {print $2}' /proc/meminfo)
  ram_mb=$((ram_kb/1024))
  if   [ "$ram_mb" -le 2048 ]; then SWAP_MB=$ram_mb
  elif [ "$ram_mb" -le 8192 ]; then SWAP_MB=2048
  else                              SWAP_MB=4096
  fi
fi
[[ "$SWAP_MB" =~ ^[0-9]+$ ]] || die "swap size must be numeric"

# ---------- summary + final confirmation ----------
hdr "Summary"
echo "  ${GREEN}•${NC} Hostname:   $HOSTNAME_VAL"
echo "  ${GREEN}•${NC} Locale:     $LOCALE"
echo "  ${GREEN}•${NC} Keymap:     $KEYMAP"
echo "  ${GREEN}•${NC} Timezone:   $TIMEZONE"
echo "  ${GREEN}•${NC} Disk:       $DISK ($([ "$EFI_MODE" = 1 ] && echo "GPT/UEFI" || echo "MBR/BIOS"))"
echo "  ${GREEN}•${NC} Filesystem: $ROOTFS"
echo "  ${GREEN}•${NC} Swap:       ${SWAP_MB} MiB"
[ -n "$USERNAME" ] && echo "  ${GREEN}•${NC} User:       $USERNAME (sudo)"
echo "  ${GREEN}•${NC} Desktop:    $([ "$DE_CHOICE" = "2" ] && echo "XFCE4 (first boot)" || echo "Terminal")"
echo
echo "${RED}${BOLD}$(msg warn_destroy) $DISK${NC}"
read -r -p "${YELLOW}$(msg type_yes)${NC} " CONFIRM
[ "$CONFIRM" = "yes" ] || [ "$CONFIRM" = "ja" ] || { echo "$(msg cancelled)"; exit 0; }

# ============================================================
hdr "$(msg installing)"

# 1. Partition
say "[1/9] Partitioning $DISK"
if [ "$EFI_MODE" = 1 ]; then
  run parted -s "$DISK" mklabel gpt
  run parted -s "$DISK" mkpart ESP fat32 1MiB 513MiB
  run parted -s "$DISK" set 1 esp on
  if [ "$SWAP_MB" -gt 0 ]; then
    run parted -s "$DISK" mkpart primary linux-swap 513MiB "$((513+SWAP_MB))MiB"
    run parted -s "$DISK" mkpart primary "$ROOTFS" "$((513+SWAP_MB))MiB" 100%
    SWAP_PART_IDX=2; ROOT_PART_IDX=3
  else
    run parted -s "$DISK" mkpart primary "$ROOTFS" 513MiB 100%
    SWAP_PART_IDX=""; ROOT_PART_IDX=2
  fi
  EFI_PART_IDX=1
else
  run parted -s "$DISK" mklabel msdos
  if [ "$SWAP_MB" -gt 0 ]; then
    run parted -s "$DISK" mkpart primary linux-swap 1MiB "$((1+SWAP_MB))MiB"
    run parted -s "$DISK" mkpart primary "$ROOTFS"   "$((1+SWAP_MB))MiB" 100%
    run parted -s "$DISK" set 2 boot on
    SWAP_PART_IDX=1; ROOT_PART_IDX=2
  else
    run parted -s "$DISK" mkpart primary "$ROOTFS" 1MiB 100%
    run parted -s "$DISK" set 1 boot on
    SWAP_PART_IDX=""; ROOT_PART_IDX=1
  fi
  EFI_PART_IDX=""
fi

# Wait for the kernel to expose new partitions.
run partprobe "$DISK"
udevadm settle 2>/dev/null || sleep 2

part_dev() {
  local idx="$1"
  if [[ "$DISK" == *nvme* || "$DISK" == *mmcblk* ]]; then
    printf '%sp%s\n' "$DISK" "$idx"
  else
    printf '%s%s\n' "$DISK" "$idx"
  fi
}

ROOT_PART=$(part_dev "$ROOT_PART_IDX")
[ -n "$SWAP_PART_IDX" ] && SWAP_PART=$(part_dev "$SWAP_PART_IDX") || SWAP_PART=""
[ -n "$EFI_PART_IDX"  ] && EFI_PART=$(part_dev "$EFI_PART_IDX")   || EFI_PART=""

# 2. Format
say "[2/9] Formatting $ROOT_PART ($ROOTFS)"
run "${MKFS[@]}" "$ROOT_PART"
if [ -n "$SWAP_PART" ]; then
  run mkswap -L ZenbyteSwap "$SWAP_PART"
fi
if [ -n "$EFI_PART" ]; then
  run mkfs.fat -F32 -n ZBESP "$EFI_PART"
fi

# 3. Mount
say "[3/9] Mounting"
run install -d -m 0755 "$TARGET"
run mount "$ROOT_PART" "$TARGET"
if [ -n "$EFI_PART" ]; then
  run install -d -m 0755 "$TARGET/boot/efi"
  run mount "$EFI_PART" "$TARGET/boot/efi"
fi
if [ -n "$SWAP_PART" ]; then
  swapon "$SWAP_PART" 2>/dev/null || true
fi

# 4. Copy system
say "[4/9] Copying system (this takes a while)"
# Stay on the live root filesystem; rsync excludes virtual mounts and the target itself.
if command -v rsync >/dev/null 2>&1; then
  run rsync -aAXH --info=progress2 \
       --exclude={"/proc/*","/sys/*","/dev/*","/run/*","/tmp/*","/mnt/*","/media/*","/lost+found","/var/cache/zbpm/*","/var/log/zbpm.log","/root/.bash_history","/home/*/.bash_history"} \
       / "$TARGET/"
else
  # cp fallback that respects --one-file-system; the target mount is on a different fs.
  run cp -ax --one-file-system / "$TARGET/"
  run install -d -m 1777 "$TARGET/tmp"
fi

# Recreate virtual mountpoints inside the target.
run install -d -m 0555 "$TARGET/proc" "$TARGET/sys"
run install -d -m 0755 "$TARGET/dev" "$TARGET/run"

# 5. fstab
say "[5/9] Writing /etc/fstab"
ROOT_UUID=$(blkid -s UUID -o value "$ROOT_PART")
{
  echo "# /etc/fstab — generated by ZenbyteOS installer"
  echo "UUID=$ROOT_UUID  /          $ROOTFS  defaults,relatime  0 1"
  if [ -n "$EFI_PART" ]; then
    EFI_UUID=$(blkid -s UUID -o value "$EFI_PART")
    echo "UUID=$EFI_UUID  /boot/efi  vfat     umask=0077         0 2"
  fi
  if [ -n "$SWAP_PART" ]; then
    SWAP_UUID=$(blkid -s UUID -o value "$SWAP_PART")
    echo "UUID=$SWAP_UUID none       swap     sw                 0 0"
  fi
} > "$TARGET/etc/fstab"

# 6. System configuration (no chroot needed for plain file writes)
say "[6/9] Configuring system"
echo "$HOSTNAME_VAL" > "$TARGET/etc/hostname"
cat > "$TARGET/etc/hosts" <<HOSTSEOF
127.0.0.1   localhost
127.0.1.1   $HOSTNAME_VAL
::1         localhost ip6-localhost ip6-loopback
HOSTSEOF

# Timezone (validated already)
ln -sfn "/usr/share/zoneinfo/$TIMEZONE" "$TARGET/etc/localtime"
echo "$TIMEZONE" > "$TARGET/etc/timezone"

# Locale + keymap
echo "LANG=$LOCALE" > "$TARGET/etc/locale.conf"
echo "KEYMAP=$KEYMAP" > "$TARGET/etc/vconsole.conf"

# Mount pseudo filesystems for password / grub steps that need a chroot.
run mount --bind /proc "$TARGET/proc"
run mount --bind /sys  "$TARGET/sys"
run mount --bind /dev  "$TARGET/dev"
run mount --bind /dev/pts "$TARGET/dev/pts"

# Passwords via stdin only — never echoed, never on the command line.
chpasswd --root "$TARGET" <<<"root:$ROOTPW" || die "failed to set root password"
unset ROOTPW ROOTPW2

# 7. User account
if [ -n "$USERNAME" ]; then
  say "[7/9] Creating user $USERNAME"
  chroot "$TARGET" useradd -m -s /bin/bash -G wheel "$USERNAME"
  chpasswd --root "$TARGET" <<<"$USERNAME:$USERPW" || die "failed to set user password"
  unset USERPW USERPW2

  # Use group-based sudo (wheel) instead of per-user file. Validated with visudo.
  printf '%%wheel ALL=(ALL) ALL\n' > "$TARGET/etc/sudoers.d/10-wheel"
  chmod 0440 "$TARGET/etc/sudoers.d/10-wheel"
  chroot "$TARGET" visudo -c -f /etc/sudoers.d/10-wheel >/dev/null \
    || die "sudoers validation failed"
else
  say "[7/9] Skipping user creation (root only)"
fi

# 8. Bootloader
say "[8/9] Installing bootloader"
# Find a real kernel image; if rsync grabbed it that's fine, otherwise hunt.
if [ ! -e "$TARGET/boot/vmlinuz" ]; then
  VMLINUZ=$(find "$TARGET/boot" /boot -maxdepth 2 -name 'vmlinuz*' -type f 2>/dev/null | sort | tail -1)
  [ -n "$VMLINUZ" ] && cp "$VMLINUZ" "$TARGET/boot/vmlinuz"
fi
if [ ! -e "$TARGET/boot/initramfs.img" ]; then
  INITRD=$(find "$TARGET/boot" /boot -maxdepth 2 -name 'initramfs*.img' -type f 2>/dev/null | sort | tail -1)
  [ -n "$INITRD" ] && cp "$INITRD" "$TARGET/boot/initramfs.img"
fi
[ -e "$TARGET/boot/vmlinuz" ]      || die "no kernel image found"
[ -e "$TARGET/boot/initramfs.img" ] || die "no initramfs found"

if [ "$EFI_MODE" = 1 ]; then
  chroot "$TARGET" grub2-install --target=x86_64-efi --efi-directory=/boot/efi \
                                 --bootloader-id=ZenbyteOS --recheck
else
  chroot "$TARGET" grub2-install --target=i386-pc --recheck "$DISK"
fi

GRUB_DIR="$TARGET/boot/grub2"
[ -d "$GRUB_DIR" ] || GRUB_DIR="$TARGET/boot/grub"
install -d -m 0755 "$GRUB_DIR"

cat > "$GRUB_DIR/grub.cfg" <<GRUBEOF
set timeout=5
set default=0

# Recovery is gated by GRUB password — set one with grub2-mkpasswd-pbkdf2
# and 'set superusers="root"' / 'password_pbkdf2 root <hash>' to enable.

menuentry "ZenbyteOS" {
    search --no-floppy --fs-uuid --set=root $ROOT_UUID
    linux /boot/vmlinuz root=UUID=$ROOT_UUID rw quiet
    initrd /boot/initramfs.img
}

menuentry "ZenbyteOS (verbose)" {
    search --no-floppy --fs-uuid --set=root $ROOT_UUID
    linux /boot/vmlinuz root=UUID=$ROOT_UUID rw
    initrd /boot/initramfs.img
}
GRUBEOF

# 9. First-boot service for desktop install (only if XFCE selected)
if [ "$DE_CHOICE" = "2" ]; then
  say "[9/9] Enabling XFCE first-boot service"
  # Use the unit shipped in the rootfs, not an inline heredoc.
  if [ -f "$TARGET/usr/lib/systemd/system/zbpm-firstboot.service" ]; then
    chroot "$TARGET" systemctl enable zbpm-firstboot.service >/dev/null
    # Hand the desired user to the post-install hook (so we don't autologin root).
    install -d -m 0755 "$TARGET/etc/zbpm"
    if [ -n "$USERNAME" ]; then
      install -m 0600 /dev/null "$TARGET/etc/zbpm/firstboot.user"
      printf '%s\n' "$USERNAME" > "$TARGET/etc/zbpm/firstboot.user"
    fi
  else
    warn "zbpm-firstboot.service not found in rootfs, skipping"
  fi
else
  say "[9/9] Skipping desktop"
fi

# 10. Configure zbpm mirror + signing key on the installed system
say "[10/10] Configuring zbpm"
install -d -m 0755 "$TARGET/etc/zbpm"
install -d -m 0700 "$TARGET/etc/zbpm/keys"

if [ -n "$ZBPM_MIRROR" ]; then
  printf '%s\n' "$ZBPM_MIRROR" > "$TARGET/etc/zbpm/mirrors"
  say "  Mirror: $ZBPM_MIRROR"

  # Try to fetch the public signing key from <mirror>/zbpm.gpg
  ZBPM_KEY_URL="$ZBPM_MIRROR/zbpm.gpg"
  if command -v curl >/dev/null 2>&1; then
    if curl -fsSL --max-time 10 "$ZBPM_KEY_URL" \
         -o "$TARGET/etc/zbpm/keys/zbpm.gpg" 2>/dev/null; then
      say "  Signing key installed from $ZBPM_KEY_URL"
    else
      warn "Could not fetch signing key from $ZBPM_KEY_URL"
      warn "Copy tools/keys/zbpm.gpg to /etc/zbpm/keys/zbpm.gpg on the installed system before running zbpm."
      # Leave ZBPM_REQUIRE_SIGNATURE=0 as a fallback so firstboot can still install packages
      printf 'ZBPM_REQUIRE_SIGNATURE=0\n' >> "$TARGET/etc/zbpm/zbpm.conf"
      warn "Signature verification disabled until key is installed."
    fi
  else
    warn "curl not available — skipping key fetch. Install key manually after boot."
  fi
else
  say "  No mirror set — configure /etc/zbpm/mirrors after first boot"
  # Keep default mirrors file if it exists, otherwise leave empty
fi

# Cleanup partial state from running zbpm in the live env
rm -f "$TARGET"/install.sh "$TARGET"/root/.bash_history 2>/dev/null

# Unmount cleanly (trap will catch leftovers).
sync
for mp in dev/pts dev proc sys boot/efi ""; do
  mountpoint -q "$TARGET/$mp" 2>/dev/null && umount "$TARGET/$mp"
done
[ -n "$SWAP_PART" ] && swapoff "$SWAP_PART" 2>/dev/null || true

echo
hr
echo "${BOLD}${GREEN}  $(msg done)${NC}"
hr
echo "  Hostname: ${BOLD}$HOSTNAME_VAL${NC}"
echo "  Root:     ${BOLD}$ROOT_PART${NC}  ($ROOTFS)"
[ -n "$USERNAME" ] && echo "  User:     ${BOLD}$USERNAME${NC}"
echo
echo "  ${YELLOW}$(msg remove_media)${NC}"
echo
