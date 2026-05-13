# Installing Platin Zenbyte

## Requirements

| | Minimum |
|---|---|
| Architecture | x86_64 |
| RAM | 512 MiB (live) / 1 GiB (installed) |
| Disk | 4 GiB |
| Firmware | UEFI or BIOS/Legacy |

## Writing the ISO

```bash
# Find your USB device (do NOT use a partition — use the whole disk)
lsblk -d -o NAME,SIZE,MODEL

# Write (replace sdX with your device)
dd if=build/zenbyte.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

Boot the target machine from the USB. On UEFI systems, select the
"UEFI: ..." entry in the firmware boot menu if given a choice.

## Boot Menu

After the kernel loads `init.cpio`, the Platin Zenbyte init script presents a
menu:

```
================================
   Welcome to ZenbyteOS
================================
  Network: online

  1) Boot system
  2) Install ZenbyteOS
  3) Emergency shell

>
```

Select **2** to start the installer. The menu also appears in German if the
GRUB entry "ZenbyteOS (German menu)" was selected, or if `lang=de` was passed
on the kernel command line.

## Installer Walkthrough

The installer (`install.sh`) walks through several screens:

### 1. Language
Choose English or German. Affects all subsequent prompts and the installed
`/etc/locale.conf`.

### 2. System info
Displays detected CPU, RAM, kernel version, architecture, and UEFI/BIOS mode.
No input required.

### 3. Hostname
Default: `zenbyte`. Must match `^[a-z0-9][a-z0-9-]{0,62}$`.

### 4. Root password
Must be at least 8 characters. Confirmed twice. Never echoed or passed on the
command line — handled via `chpasswd` stdin.

### 5. User account (optional)
Creates an unprivileged user in the `wheel` group (sudo access). Leave blank
for a root-only system.

### 6. Timezone
Presets: `Europe/Berlin`, `Europe/Vienna`, `Europe/Zurich`, `UTC`, or enter
any valid zoneinfo path (e.g. `America/New_York`).

### 7. Locale
`en_US.UTF-8` (default), `de_DE.UTF-8`, or `C.UTF-8`.

### 8. Keyboard layout
`us` (default), `de`, or `uk`.

### 9. Package repository
Enter the URL of a `zbpm-repo serve` instance (e.g. `http://192.168.1.10:8765`).
The installer will try to fetch the signing key from `<mirror>/zbpm.gpg`.

Leave blank to configure `/etc/zbpm/mirrors` manually after first boot.

### 10. Desktop environment
| Choice | Result |
|---|---|
| 1 — Terminal only | No desktop. Boots to a systemd text session. |
| 2 — XFCE4 | Enables `zbpm-firstboot.service`; XFCE4 is installed on first boot via `zbpm`. Requires a mirror to be configured. |
| 3 — Skip | Configure later with `zbpm install xfce4-desktop`. |

### 11. Target disk
Lists available disks via `lsblk`. Enter the device name (e.g. `sda`,
`nvme0n1`). The disk must be at least 4 GiB and must not have any partition
currently mounted.

### 12. Root filesystem
| Choice | Notes |
|---|---|
| ext4 (default) | Best-tested, most compatible. |
| btrfs | Subvolumes and snapshots supported after install. |
| xfs | Good for large files and high-throughput workloads. |

### 13. Swap
Enter size in MiB, or leave blank for automatic sizing:

- ≤ 2 GiB RAM → swap = RAM size
- ≤ 8 GiB RAM → swap = 2 GiB
- \> 8 GiB RAM → swap = 4 GiB

Enter `0` for no swap.

### 14. Summary + final confirmation

The installer prints a summary of all choices and asks you to type `yes` (or
`ja` in German) to confirm. **This is the last chance to abort — after this
point all data on the target disk will be erased.**

## What the Installer Does

```
[1/9]  Partition disk          parted (GPT+ESP for UEFI, MBR for BIOS)
[2/9]  Format partitions       mkfs.ext4 / mkfs.btrfs / mkfs.xfs + mkswap
[3/9]  Mount                   /mnt/zenbyte (+ /boot/efi for UEFI)
[4/9]  Copy system             rsync -aAXH (or cp -ax as fallback)
[5/9]  Write /etc/fstab        UUID-based entries for root, EFI, swap
[6/9]  Configure system        hostname, hosts, timezone, locale, keymap,
                               root password via chpasswd
[7/9]  User account            useradd + chpasswd + sudoers
[8/9]  Bootloader              grub2-install + grub.cfg with UUID root=
[9/9]  Desktop / firstboot     zbpm-firstboot.service (XFCE4 path only)
[10/10] zbpm mirror + key      /etc/zbpm/mirrors, fetches zbpm.gpg
```

On completion, all mounts are cleanly unmounted. Remove the install media and
reboot.

## After First Boot

```bash
# Sync the package index from your mirror
zbpm sync

# Install packages
zbpm install nano htop

# Upgrade all installed packages
zbpm upgrade

# Install XFCE4 manually (if skipped during install)
zbpm install xfce4-desktop
```

See `zbpm help` for the full command reference.

## Troubleshooting

**chroot: failed to run command '/bin/bash': Permission denied**
The rootfs shipped in this repo requires a host with `core.fileMode=true` and
`core.symlinks=true` (the default on Linux). On Windows/MSYS2 Git hosts these
options are often disabled, causing execute bits and symlinks to be lost. Pull
on a Linux machine and run `git config core.fileMode true core.symlinks true`
before checking out.

**Network is offline at the boot menu**
The init script tries DHCP for 10 seconds on the first non-loopback interface.
It will fail gracefully and still present the menu. You can configure networking
manually from the emergency shell (`/sbin/ip`, `dhclient`).

**grub2-install fails (UEFI)**
Make sure the EFI partition is mounted and `efibootmgr` is available on the
live system. The installer mounts it at `/boot/efi` automatically.
