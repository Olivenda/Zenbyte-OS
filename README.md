<h1 align="center">Zenbite</h1>
<p align="center">
  <em>A from-scratch 32-bit retro operating system, in the spirit of MS-DOS / FreeDOS.</em>
</p>

<p align="center">
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-MIT-blue?style=for-the-badge"></a>
  <a href="https://github.com/olivenda/zenbite-/releases"><img alt="Release" src="https://img.shields.io/badge/release-v3.1-darkorange?style=for-the-badge"></a>
  <a href="https://github.com/olivenda/zenbite-/actions"><img alt="Build" src="https://img.shields.io/badge/build-passing-success?style=for-the-badge"></a>
  <a href="https://github.com/olivenda/zenbite-/stargazers"><img alt="Stars" src="https://img.shields.io/badge/stars-%E2%98%85-yellow?style=for-the-badge"></a>
  <a href="https://github.com/olivenda/zenbite-/issues"><img alt="Good first issues" src="https://img.shields.io/badge/good%20first%20issue-welcome-brightgreen?style=for-the-badge"></a>
</p>

<p align="center">
  <strong>Boots from a CD or floppy &middot; runs in 4 MiB of RAM &middot; ships with a real network stack &middot; installs from a wizard &middot; two desktops: a classic text-mode WM and the new <em>Slate</em> pixel desktop at 800x600 / 1024x768 / 1280x720.</strong>
</p>

---

```
   ____             _     _ _
  |_  / ___ _ __  | |__ (_) |_ ___    .   ✦    ✧
   / / / _ \ '_ \ | '_ \| | __/ _ \    ✦   .   ✧
  / /_|  __/ | | || |_) | | ||  __/   ✧ ✦   .
 /____\___|_| |_||_.__/|_|\__\___|     .  ✦ ✧

Zenbite 3.1 -- Slate
(c) 2026 Zenbite contributors -- MIT License

CPU   : AuthenticAMD  64-bit capable -- running in 32-bit compatibility mode
Memory: 15998 KiB total, 834 KiB used (kernel), 14524 KiB free
net   : MAC 52:54:00:12:34:56  IP 10.0.2.16

A:\> gdesk         # pixel desktop in graphics mode (800x600 default)
A:\> desktop       # classic text-mode desktop (always works)
A:\>
```

## Features

| Subsystem      | What you get                                                                       |
| -------------- | ---------------------------------------------------------------------------------- |
| Bootloader     | Custom two-stage MBR + stage2 (FAT12 floppy, FAT16/FAT32 HDD, BPB-aware + LBA)     |
| Kernel         | 32-bit PM, GDT/IDT/PIC/PIT, no paging on 386/486, PSE 4 MiB pages on Pentium+      |
| Display        | VGA text mode (80x25 / 80x50), Bochs VBE framebuffer 640x480 / 800x600 / 1024x768 / 1280x720 |
| Input          | PS/2 keyboard (US + DE QWERTZ) + PS/2 mouse, **USB 1.1 (UHCI) + USB 2.0 (EHCI)** HID + mass storage |
| Storage        | ATA PIO + AHCI (LBA48 to 128 GiB), floppy FDC, FAT12 / FAT16 / FAT32, MBR partitions + view-disks |
| Networking     | Intel e1000 + NE2000 drivers, ARP, IPv4, ICMP, UDP, TCP, DNS, HTTP                 |
| Time           | CMOS RTC + PIT @ 100 Hz; `date` and `time` builtins                                |
| Shell          | DOS-style REPL: `?`, `ver`, `dir`, `cd`, `type`, `copy`, `del`, `cc`, `run`, `gdesk`, `mkzbx` ... |
| Apps           | Files Commander, Editor, Tetris, Minesweeper, Snake, Analog Clock, Net Scanner, Partition Manager, Web, Disk Manager, Settings, Notes, Calendar, Terminal, System Info, Number Game |
| Installer      | **Fully graphical** wizard: pick disk -> format -> bootloader -> user -> samples (never drops to text mode) |
| Classic desktop| Cell-based windowed UI with categorised launcher, taskbar + tray, autostart, themes |
| Slate desktop  | **NEW in 3.1** -- pixel-rendered Win95-feel desktop with Zenbite branding (gdesk)  |
| Terminal       | **Virtual shell** with path prompt, command history, tab autocomplete, fullscreen app support |
| Scheduler      | **Cooperative multitasking** -- apps yield CPU, desktop stays responsive |
| Executables    | `.ZBX` Zenbite executables -- C subset with windowing API; bundled apps + samples  |

## Quick start

```sh
sudo apt install build-essential gcc-multilib nasm qemu-system-x86 mtools dosfstools
make            # CD primary build: floppy + setup + CD + install + blank target
make floppyos   # floppy-only build (no CD), Zenbite 2.0 baseline behaviour
make cd         # CD-only build
make run        # boots all the disks in QEMU
make test       # headless boot for CI -- scrapes serial for ZENBITE READY
make clean
```

Inside the OS:

```
A:\> gdesk            # Slate pixel desktop in graphics mode
A:\> desktop          # classic text-mode desktop with categorised launcher (F9)
A:\> run HELLO.ZBX    # run a Zenbite native executable
A:\> mkzbx hi.c HI.ZBX -f   # package a C source as a fullscreen .ZBX
A:\> app list         # list installed apps
A:\> app install GAME.ZBX    # install an app
A:\> ping 10.0.2.2          # ICMP echo, 4 packets
A:\> wget http://10.0.2.2/  # fetch a URL
A:\> ifconfig               # MAC + IP + gateway
A:\> keymap de              # switch to QWERTZ
A:\> date && time           # real wall clock from CMOS
A:\> parts hda              # MBR partition table
```

Terminal features (in Slate desktop):
```
A:\SYSTEM\BIN> ls      # path-aware prompt
A:\SYSTEM\BIN> evi     # runs editor (non-blocking with scheduler)
Arrow Up/Down          # command history
Tab                    # autocomplete
help                   # terminal help
cls / clear            # clear screen
```

Boot media:

```sh
# CD / USB (recommended for v3.1):
sudo dd if=zenbite_usb.img of=/dev/sdX bs=4M conv=fsync     # USB stick
# or burn zenbite_install_cd.img to a CD/DVD

# Floppy (Zenbite 2.0 compat path):
sudo dd if=zenbite.img of=/dev/fd0 bs=512 conv=fsync
```

## Disk layout

```
zenbite.img            1.44 MiB FAT12 boot floppy (stage1+stage2+kernel; self-installable)
zenbite_setup.img      1.44 MiB FAT12 setup floppy (SYSTEM/, BOOT/, SAMPLES/)
zenbite_install1.img   8 MiB FAT16 setup disk 1 (system files)
zenbite_install2.img   8 MiB FAT16 setup disk 2 (samples)
zenbite_install_cd.img 16 MiB combined install CD (everything in one image, v3.1 default)
zenbite_usb.img        32 MiB partitioned USB image (MBR + FAT16 partition)
zenbite_target.img     64 MiB blank ATA target (formatted by the installer)
```

The installer creates a home folder for the user it asks you to name:

```
C:\HOME\<USER>\
  DESKTOP\
  DOCUMENTS\
  DOWNLOADS\
  PICTURES\
  PROFILE.TXT     # username, created-at, machine name
```

The primary user has unrestricted access to every drive and folder — Zenbite's permission model is intentionally Linux-root-equivalent for the main account.

## Writing your own apps

Zenbite ships a tiny C compiler (`zbc`) and a windowed runtime so you can write
real apps that run on the OS. The format is `.ZBX` -- a C source with an
8-byte header. Builders:

```
mkzbx hello.c HELLO.ZBX            # console app
mkzbx mywin.c MYWIN.ZBX -f         # fullscreen TUI / windowed app
run HELLO.ZBX                       # invoke from the shell
```

A 5-second windowed example, in the zbc subset:

```c
int main() {
    cls(0x11);                                /* fill screen, slate blue */
    window(6, 18, 44, 8, "Hello");           /* draw a window */
    at_puts(9, 21, 0x0F, "Hi from .ZBX!");   /* white text inside */
    button(13, 32, 11, 0x0F, 1);             /* a 3D button */
    at_puts(13, 35, 0x0F, "OK");
    present();
    waitkey();
    return 0;
}
```

The full reference -- runtime API, format spec, mouse + file I/O, full
worked tutorials, common gotchas -- lives in [`APPS.md`](APPS.md). Each
shipped Zenbite app under `\SYSTEM\BIN\*.ZBX` is also a reading sample.

## New in v3.2

### Virtual Terminal
- Path-aware prompt: `A:\SYSTEM\BIN>`
- Command history (Arrow Up/Down)
- Tab autocomplete for files/directories
- Built-in commands: `help`, `cls`, `clear`
- Fullscreen apps run inside the terminal

### Cooperative Scheduler
- `proc_yield()` lets apps share CPU time
- Desktop stays responsive while apps run
- `proc_sleep()` for timed delays

### Fully Graphical Installer
- Never drops to text mode
- Pixel-rendered disk picker
- Real-time progress with log
- Auto-mounts installed drive as A:

### New Apps
- SYSINFO - System information display
- GUESS2 - Number guessing game
- MINES - Simple minesweeper
- More apps in `\SYSTEM\BIN\`

### System Monitor
- Shows total RAM (not just kernel heap)
- Live usage graph
- Uptime display

### Development Header
- `zenbite.h` - Standard header for .ZBX development
- Type definitions, key codes, color constants
- Helper functions for drawing, math, random numbers

## Roadmap

- **v0.1 — It boots and types** ✓ bootloader + 32-bit kernel + VGA + PS/2 + FAT12 + shell
- **v0.2 — It remembers things** ✓ FAT16 write, editor, ZAS assembler, mini C compiler
- **v0.3 — It connects and clicks** ✓ TCP/IP, e1000, AHCI, FAT32 ≤ 4 GiB, mouse, Desktop
- **v2.0 — It runs apps** ✓ install wizard, partition mgr, .ZBX executables, USB HID + EHCI
- **v3.1 — It looks like an OS** ✓ Slate pixel desktop, boot splash, more themes, security panel, CD-primary build
- **v3.2** ✓ Fully graphical installer, virtual terminal with autocomplete, cooperative scheduler, system monitor, new apps
- **v3.3** Better editor, more .ZBX apps, HDMI support, sound drivers
- **v4.0** ring-3 userland + syscall ABI; sound (PC speaker / SB16 / Realtek); long filenames

## Contributing

Pull requests welcome from absolutely anyone. Zenbite is MIT — do whatever you want with
it, just keep the credit line. Start with [`CONTRIBUTING.md`](CONTRIBUTING.md). Good first
issues live on the [issue tracker](https://github.com/olivenda/zenbite-/issues) with the
`good-first-issue` label.

## Maintainers

- **Oliver Petz** — project lead and primary maintainer ([@olivenda](https://github.com/olivenda))

Past and present contributors are recorded in [`AUTHORS`](AUTHORS) and in the git history
(`git shortlog -sne`).

## License

[MIT](LICENSE) — © 2026 Oliver Petz and Zenbite contributors.
