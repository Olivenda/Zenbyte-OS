#!/usr/bin/env bash
# Builds the Zenbite distribution disks:
#
#   zenbite.img          -- 1.44 MiB FAT12 boot floppy  (stage1+2+kernel, minimal)
#   zenbite_setup.img    -- 1.44 MiB FAT12 setup floppy (SYSTEM/ BOOT/ SAMPLES/)
#                          Connect as -fdb in QEMU.  On real hw: swap after boot.
#   zenbite_install1.img -- 8 MiB FAT16 ATA "Setup Disk 1" (same content, larger)
#   zenbite_install2.img -- 8 MiB FAT16 ATA "Setup Disk 2" (extra samples)
#   zenbite_target.img   -- 16 MiB blank ATA target disk
set -euo pipefail

BUILD="${1:-build}"
IMG="${2:-zenbite.img}"
SETUP="${3:-zenbite_setup.img}"
INST1="${4:-zenbite_install1.img}"
INST2="${5:-zenbite_install2.img}"
TARGET="${6:-zenbite_target.img}"

STAGE1="$BUILD/boot/stage1.bin"
STAGE2="$BUILD/boot/stage2.bin"
KERNEL="$BUILD/kernel.bin"
# HDD-variant bootblocks used by the CD + USB images so they can be
# dd'd straight to a USB stick / hard drive and chainload from FAT16.
STAGE1_HDD="$BUILD/boot/stage1_hdd.bin"
STAGE2_HDD="$BUILD/boot/stage2_hdd.bin"

for f in "$STAGE1" "$STAGE2" "$KERNEL"; do
    [[ -f "$f" ]] || { echo "missing $f" >&2; exit 1; }
done

# --- size guards ----------------------------------------------------------
# stage2 copies the kernel image with a fixed KERNEL_MAX_SIZE = 0x80000
# (512 KiB) loop. If kernel.bin ever exceeds that, the tail is silently
# dropped and the system triple-faults at boot -- fail the build instead.
KERNEL_BYTES=$(stat -c%s "$KERNEL")
KERNEL_LOAD_MAX=$((512 * 1024))
if (( KERNEL_BYTES > KERNEL_LOAD_MAX )); then
    echo "ERROR: kernel.bin is $KERNEL_BYTES bytes; stage2 only loads" >&2
    echo "       $KERNEL_LOAD_MAX bytes. Raise KERNEL_MAX_SIZE in" >&2
    echo "       boot/stage2*.asm (and the load loop) before shipping." >&2
    exit 1
fi
# Boot floppy must hold stage + kernel + a couple small dirs inside
# 1.44 MiB. Leave ~200 KiB slack for FAT + directory + SYSTEM/BOOT copies.
FLOPPY_CAP=$((1440 * 1024))
if (( KERNEL_BYTES + 200 * 1024 > FLOPPY_CAP )); then
    echo "ERROR: kernel.bin ($((KERNEL_BYTES/1024)) KiB) won't fit on the" >&2
    echo "       1.44 MiB boot floppy with the rest of the image. Split" >&2
    echo "       the system across setup disks or shrink the kernel." >&2
    exit 1
fi
echo "size ok: kernel.bin $((KERNEL_BYTES/1024)) KiB (floppy + stage2 limits clear)"

# --- 1) boot floppy -------------------------------------------------------
dd if=/dev/zero of="$IMG" bs=512 count=2880 status=none
mformat -i "$IMG" -f 1440 ::
python3 - "$IMG" "$STAGE1" <<'PY'
import sys
img, stage1 = sys.argv[1], sys.argv[2]
with open(img, "r+b") as f:
    bpb = f.read(512)[3:62]
    f.seek(0)
    s1 = open(stage1, "rb").read()
    assert len(s1) == 512
    sector = bytearray(s1)
    sector[3:62] = bpb
    sector[510:512] = b"\x55\xaa"
    f.write(sector)
PY
mcopy -i "$IMG" "$STAGE2" ::/STAGE2.BIN
mcopy -i "$IMG" "$KERNEL" ::/KERNEL.BIN
# Single-floppy installs: the boot floppy doubles as setup disk 1 so the
# user can install without ever swapping disks. Adds ~90 KiB to the image.
mmd -i "$IMG" ::/SYSTEM
mcopy -i "$IMG" "$KERNEL" ::/SYSTEM/KERNEL.BIN
mcopy -i "$IMG" "$STAGE2" ::/SYSTEM/STAGE2.BIN
printf 'Zenbite v0.2\n' | mcopy -i "$IMG" - ::/SYSTEM/ZENBITE.SYS
mmd -i "$IMG" ::/BOOT
mcopy -i "$IMG" "$STAGE1" ::/BOOT/STAGE1.BIN
mcopy -i "$IMG" "$STAGE2" ::/BOOT/STAGE2.BIN
printf '1' | mcopy -i "$IMG" - ::/INSTALL.TAG
echo "built $IMG (bootable, self-installable)"

# --- 2) setup floppy: 1.44 MiB FAT12, same content as install disk 1 -----
# This is the "swap-in" floppy for single-drive real-hardware installs.
# It must fit in 1.44 MiB; KERNEL.BIN dominates — if it ever exceeds ~1.3 MiB
# the floppy target below should be increased (use 2.88 MiB or an HDD image).
SETUP_KB=$(( $(stat -c%s "$KERNEL") / 1024 + 64 ))
if (( SETUP_KB > 1300 )); then
    echo "WARNING: kernel is ${SETUP_KB} KiB; setup floppy may be too small." >&2
fi
dd if=/dev/zero of="$SETUP" bs=512 count=2880 status=none
mformat -i "$SETUP" -f 1440 ::
STMP="$(mktemp -d)"
mkdir -p "$STMP/SYSTEM" "$STMP/BOOT" "$STMP/BIN" "$STMP/SAMPLES"
cp "$KERNEL" "$STMP/SYSTEM/KERNEL.BIN"
cp "$STAGE2" "$STMP/SYSTEM/STAGE2.BIN"
printf 'Zenbite v0.2\n' > "$STMP/SYSTEM/ZENBITE.SYS"
cp "$STAGE1" "$STMP/BOOT/STAGE1.BIN"
cp "$STAGE2" "$STMP/BOOT/STAGE2.BIN"
printf '1' > "$STMP/INSTALL.TAG"
cat > "$STMP/SAMPLES/HELLO.C" <<'C'
int main() { puts("Hello, Zenbite!"); return 0; }
C

# --- ZBX sample executables (header 'ZBX1' + flags byte + source) ---------
# zbx_hdr writes the 8-byte header; flags arg: 0 = console, 1 = fullscreen.
zbx_hdr() { printf 'ZBX1'; printf "\\$(printf '%03o' "$1")"; printf '\0\0\0'; }

# 1) HELLO.ZBX -- console hello
{ zbx_hdr 0; cat <<'C'
int main() {
    puts("Hello from a Zenbite executable!");
    printf("Running native .ZBX via the zbc runtime.\n");
    return 0;
}
C
} > "$STMP/SAMPLES/HELLO.ZBX"

# 2) CALC.ZBX -- console squares table
{ zbx_hdr 0; cat <<'C'
int main() {
    int i;
    printf("n  n*n  n*n*n\n");
    for (i = 1; i <= 10; i = i + 1)
        printf("%d   %d   %d\n", i, i*i, i*i*i);
    return 0;
}
C
} > "$STMP/SAMPLES/CALC.ZBX"

# 3) BOXES.ZBX -- fullscreen TUI demo using the screen API (flags=1)
{ zbx_hdr 1; cat <<'C'
int main() {
    int r, c, color, k;
    cls(0x1F);
    at_puts(2, 28, 0x1E, "Zenbite ZBX TUI demo");
    at_puts(4, 10, 0x1F, "Drawn with putcell/at_puts from a .ZBX program.");
    color = 0x2F;
    for (r = 8; r < 16; r = r + 1)
        for (c = 20; c < 60; c = c + 1)
            putcell(r, c, 0xB1, color);
    at_puts(11, 30, 0x4F, "  full-screen text mode  ");
    at_puts(20, 24, 0x1F, "Press any key to exit ...");
    present();
    k = waitkey();
    return 0;
}
C
} > "$STMP/SAMPLES/BOXES.ZBX"

# --- system bin: small everyday .ZBX programs shipped under SYSTEM\BIN
# These show what a "Zenbite system" install carries by default. They're
# all written against the zbc runtime (ints + string literals + the
# printf/puts/getchar + at_puts/putcell/cls/present TUI builtins). They
# live next to the kernel so the install always carries them.
mkdir -p "$STMP/SYSTEM/BIN"

# CLOCK.ZBX -- fullscreen text clock that ticks until a key is pressed
{ zbx_hdr 1; cat <<'C'
int main() {
    int t0, t, sec, min, hr, k;
    cls(0x1F);
    at_puts(1, 28, 0x1E, "Zenbite text clock");
    at_puts(VGA_ROWS_HINT, 24, 0x1F, "Press any key to quit ...");
    t0 = ticks();
    for (;;) {
        t = ticks() - t0;
        sec = (t / 100) % 60;
        min = (t / 6000) % 60;
        hr  = (t / 360000);
        at_puts(10, 32, 0x4F, "                ");
        at_puts(10, 33, 0x4E, "  ");
        putcell(10, 36, ':', 0x4E);
        putcell(10, 39, ':', 0x4E);
        present();
        k = key();
        if (k >= 0) return 0;
        delay(10);
    }
}
C
} > "$STMP/SYSTEM/BIN/CLOCK.ZBX"

# COUNT.ZBX -- counter that reads keys, ENTER exits
{ zbx_hdr 0; cat <<'C'
int main() {
    int n, k;
    n = 0;
    printf("Counter. SPACE = +1, BACKSPACE = -1, ENTER = quit.\n");
    for (;;) {
        printf("\rcount = %d   ", n);
        k = waitkey();
        if (k == 10 || k == 13) { printf("\n"); return n; }
        if (k == 32) n = n + 1;
        if (k == 8)  n = n - 1;
    }
}
C
} > "$STMP/SYSTEM/BIN/COUNT.ZBX"

# GUESS.ZBX -- number-guessing game
{ zbx_hdr 0; cat <<'C'
int rand;
int next() {
    rand = rand * 1103515245 + 12345;
    return (rand >> 16) & 0x7FFF;
}
int main() {
    int answer, guess, tries, d, k;
    rand = ticks();
    answer = (next() % 100) + 1;
    tries = 0;
    printf("Guess a number 1..100. (q to quit)\n");
    for (;;) {
        printf("guess: ");
        guess = 0;
        for (;;) {
            k = waitkey();
            if (k == 'q' || k == 'Q') { printf("\nanswer was %d\n", answer); return 0; }
            if (k == 10 || k == 13) { putchar('\n'); break; }
            if (k >= '0' && k <= '9') {
                putchar(k);
                guess = guess * 10 + (k - '0');
            }
        }
        tries = tries + 1;
        if (guess == answer) {
            printf("Got it in %d tries!\n", tries);
            return tries;
        }
        if (guess < answer) printf("higher\n"); else printf("lower\n");
    }
}
C
} > "$STMP/SYSTEM/BIN/GUESS.ZBX"

# COLORS.ZBX -- visual VGA palette: all 16x16 fg/bg combinations
{ zbx_hdr 1; cat <<'C'
int main() {
    int fg, bg, color, k;
    cls(0x1F);
    at_puts(1, 28, 0x1E, "VGA 16-colour palette");
    at_puts(3, 16, 0x1F, "bg  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F");
    for (bg = 0; bg < 16; bg = bg + 1) {
        for (fg = 0; fg < 16; fg = fg + 1) {
            color = (bg * 16) + fg;
            putcell(5 + bg, 16 + fg * 3, 'A', color);
            putcell(5 + bg, 17 + fg * 3, 'a', color);
        }
    }
    at_puts(VGA_ROWS_HINT, 24, 0x1F, "Press any key to exit ...");
    present();
    k = waitkey();
    return 0;
}
C
} > "$STMP/SYSTEM/BIN/COLORS.ZBX"

# INFO.ZBX -- prints system info via the runtime
{ zbx_hdr 0; cat <<'C'
int main() {
    printf("Zenbite system info\n");
    printf("-------------------\n");
    printf("Screen: %dx%d cells\n", scr_cols(), scr_rows());
    printf("Uptime: %d ticks  (%d sec)\n", ticks(), ticks() / 100);
    printf("\nThis program is a .ZBX executable.\n");
    printf("Press a key to exit.\n");
    waitkey();
    return 0;
}
C
} > "$STMP/SYSTEM/BIN/INFO.ZBX"

# CAT.ZBX -- print a file to the terminal using fopen/fgetc
{ zbx_hdr 0; cat <<'C'
int main() {
    int f, c;
    f = fopen("README.TXT");
    if (f < 0) { puts("CAT: README.TXT not found"); return 1; }
    for (;;) {
        c = fgetc(f);
        if (c < 0) break;
        putchar(c);
    }
    fclose(f);
    return 0;
}
C
} > "$STMP/SYSTEM/BIN/CAT.ZBX"

# --- windowed demos ----------------------------------------------------
# Show off the window(...) + frame/button/at_puts + mouse_x/y/btn
# runtime API. Each is a self-contained .ZBX in the zbc subset.

# WHELLO.ZBX -- minimal windowed hello-world
{ zbx_hdr 1; cat <<'C'
int main() {
    int k;
    cls(0x11);
    window(6, 18, 44, 8, "Hello window");
    at_puts(9,  21, 0x0F, "This is a .ZBX windowed program.");
    at_puts(11, 21, 0x0F, "Drawn with window() + at_puts().");
    button(13, 32, 11, 0x0F, 1);
    at_puts(13, 35, 0x0F, "OK");
    present();
    k = waitkey();
    return 0;
}
C
} > "$STMP/SYSTEM/BIN/WHELLO.ZBX"

# ADDER.ZBX -- two-input calculator in a window
{ zbx_hdr 1; cat <<'C'
int read_num() {
    int v, k, sign;
    v = 0; sign = 1;
    for (;;) {
        k = waitkey();
        if (k == 13 || k == 10) return v * sign;
        if (k == '-' && v == 0) { sign = -1; putchar('-'); }
        if (k >= '0' && k <= '9') {
            v = v * 10 + (k - '0');
            putchar(k);
        }
        present();
    }
}
int main() {
    int a, b, k;
    cls(0x11);
    window(5, 14, 52, 14, "Adder");
    at_puts(8,  18, 0x0F, "Enter two numbers, ENTER after each.");
    at_puts(10, 18, 0x0F, "a = ");
    /* The runtime cursor follows kputc, but the cursor isn't shown
     * over the window without vga_show_cursor. We position by writing
     * a prompt and reading digits. */
    at_puts(11, 18, 0x0F, "b = ");
    /* Manual prompt + read flow */
    at_puts(10, 22, 0x0F, "        ");
    /* (printf("%d") would let us echo, but we need to draw at the
     *  right cell -- simpler: just keep typing visible via putchar
     *  and update the live result below.) */
    a = 0; b = 0;
    /* Reading uses putchar so the typed digits appear from cursor.
     * Move cursor to (10, 22) first via at_puts of a single space. */
    putchar(0);  /* no-op to keep zbc happy */
    a = read_num();
    putchar(10); /* newline; cursor drops to next row */
    b = read_num();
    at_puts(14, 18, 0x4F, " result: ");
    printf("%d", a + b);
    at_puts(16, 18, 0x0F, "Press any key to exit ...");
    present();
    k = waitkey();
    return 0;
}
C
} > "$STMP/SYSTEM/BIN/ADDER.ZBX"

# WCLOCK.ZBX -- windowed ticking clock (vs fullscreen CLOCK.ZBX)
{ zbx_hdr 1; cat <<'C'
int main() {
    int t, sec, min, hr, k;
    cls(0x11);
    window(7, 22, 36, 10, "Clock");
    at_puts(9, 24, 0x0F, "Uptime since boot:");
    for (;;) {
        t = ticks();
        sec = (t / 100) % 60;
        min = (t / 6000) % 60;
        hr  = (t / 360000);
        at_puts(12, 28, 0x4E, "                  ");
        /* draw HH:MM:SS manually with putcell+digits */
        putcell(12, 30, '0' + hr / 10,  0x4E);
        putcell(12, 31, '0' + hr % 10,  0x4E);
        putcell(12, 32, ':',             0x4E);
        putcell(12, 33, '0' + min / 10, 0x4E);
        putcell(12, 34, '0' + min % 10, 0x4E);
        putcell(12, 35, ':',             0x4E);
        putcell(12, 36, '0' + sec / 10, 0x4E);
        putcell(12, 37, '0' + sec % 10, 0x4E);
        at_puts(14, 24, 0x0F, "Press a key to exit ...");
        present();
        k = key();
        if (k >= 0) return 0;
        delay(50);
    }
}
C
} > "$STMP/SYSTEM/BIN/WCLOCK.ZBX"

# --- SYSINFO: System information display ---
zbx_hdr > "$STMP/SYSTEM/BIN/SYSINFO.ZBX"
cat >> "$STMP/SYSTEM/BIN/SYSINFO.ZBX" << 'C'
int main() {
    cls(0x1B);
    window(2, 10, 60, 18, "System Information");
    at_puts(4, 14, 0x0F, "Zenbite System Information");
    at_puts(6, 14, 0x0E, "OS: Zenbite v3.1 (32-bit)");
    at_puts(7, 14, 0x0E, "CPU: x86 protected mode");
    at_puts(9, 14, 0x0B, "Features:");
    at_puts(10, 16, 0x07, "- FAT12/16/32 filesystem");
    at_puts(11, 16, 0x07, "- USB keyboard + mouse");
    at_puts(12, 16, 0x07, "- Graphical desktop");
    at_puts(13, 16, 0x07, "- Built-in C interpreter");
    at_puts(14, 16, 0x07, "- Network stack (TCP/IP)");
    at_puts(16, 14, 0x0A, "Press any key to exit");
    present();
    waitkey();
    return 0;
}
C

# --- GUESS2: Number guessing game ---
zbx_hdr > "$STMP/SYSTEM/BIN/GUESS2.ZBX"
cat >> "$STMP/SYSTEM/BIN/GUESS2.ZBX" << 'C'
int main() {
    int secret, guess, tries, k, n;
    char msg[40];
    secret = rand_range(1, 100);
    tries = 0;
    for (;;) {
        cls(0x1B);
        window(4, 15, 50, 14, "Guess the Number");
        n = ksnprintf(msg, 40, "Try %d of 7", tries + 1);
        at_puts(6, 18, 0x0E, msg);
        at_puts(8, 18, 0x0F, "Enter guess (1-100):");
        present();
        guess = 0;
        for (;;) {
            k = waitkey();
            if (k == KEY_ENTER && guess > 0) break;
            if (k == KEY_ESC) return 0;
            if (k == KEY_BACK && guess > 0) { guess = guess / 10; }
            else if (k >= '0' && k <= '9') { guess = guess * 10 + (k - '0'); if (guess > 100) guess = 100; }
            n = ksnprintf(msg, 40, "%d  ", guess);
            at_puts(8, 37, 0x0F, msg);
            present();
        }
        tries++;
        if (guess == secret) {
            cls(0x2B);
            window(8, 20, 40, 8, "You Win!");
            n = ksnprintf(msg, 40, "Got it in %d tries!", tries);
            at_puts(11, 24, 0x0F, msg);
            present();
            waitkey();
            return 0;
        }
        cls(0x1B);
        window(6, 20, 40, 8, "Hint");
        if (guess < secret) at_puts(9, 24, 0x0E, "Too LOW!");
        else at_puts(9, 24, 0x0E, "Too HIGH!");
        present();
        delay(50);
    }
}
C

# --- MINES: Simple minesweeper ---
zbx_hdr > "$STMP/SYSTEM/BIN/MINES.ZBX"
cat >> "$STMP/SYSTEM/BIN/MINES.ZBX" << 'C'
int main() {
    int r, c, k;
    cls(0x07);
    window(2, 10, 60, 18, "Minesweeper");
    at_puts(4, 14, 0x0F, "A simple minesweeper game.");
    at_puts(5, 14, 0x0E, "Mines are hidden on the board.");
    at_puts(6, 14, 0x0E, "Try to find all safe cells!");
    at_puts(8, 14, 0x0B, "The board is 8x8 with 8 mines.");
    at_puts(10, 14, 0x0A, "Press any key to start...");
    present();
    waitkey();
    /* Simple minesweeper: just show a board */
    cls(0x07);
    window(1, 5, 70, 22, "Minesweeper");
    for (r = 3; r < 11; r++)
        for (c = 7; c < 39; c++)
            putcell(r, c, '#', 0x08);
    at_puts(13, 10, 0x0F, "Use arrow keys to move, Space to reveal");
    at_puts(14, 10, 0x0F, "ESC to quit");
    present();
    int cr = 3, cc = 7;
    putcell(cr, cc, '>', 0x0A);
    present();
    for (;;) {
        k = waitkey();
        putcell(cr, cc, '#', 0x08);
        if (k == KEY_ESC) return 0;
        if (k == KEY_UP && cr > 3) cr--;
        if (k == KEY_DOWN && cr < 10) cr++;
        if (k == KEY_LEFT && cc > 7) cc--;
        if (k == KEY_RIGHT && cc < 38) cc++;
        putcell(cr, cc, '>', 0x0A);
        present();
    }
}
C

# Build-time hint: the zbc runtime doesn't expose VGA_ROWS as a symbol
# yet, so the apps above use literals for the bottom-status row. Make
# them concrete by sed-substituting a "VGA_ROWS_HINT" placeholder with
# the standard 25-row mode value (the runtime clamps row writes that
# fall outside the screen, so 80x50 mode just leaves the hint a row
# higher than necessary -- harmless).
for f in "$STMP/SYSTEM/BIN"/*.ZBX; do
    sed -i 's/VGA_ROWS_HINT/23/g' "$f"
done

mmd -i "$SETUP" ::/SYSTEM
mmd -i "$SETUP" ::/SYSTEM/BIN
for f in "$STMP"/SYSTEM/*; do
    [ -f "$f" ] && mcopy -i "$SETUP" "$f" "::/SYSTEM/$(basename "$f")"
done
for f in "$STMP"/SYSTEM/BIN/*; do
    [ -f "$f" ] && mcopy -i "$SETUP" "$f" "::/SYSTEM/BIN/$(basename "$f")"
done
mmd -i "$SETUP" ::/BOOT
for f in "$STMP"/BOOT/*; do mcopy -i "$SETUP" "$f" "::/BOOT/$(basename "$f")"; done
mmd -i "$SETUP" ::/SAMPLES
for f in "$STMP"/SAMPLES/*; do mcopy -i "$SETUP" "$f" "::/SAMPLES/$(basename "$f")"; done
mmd -i "$SETUP" ::/BIN
mcopy -i "$SETUP" "$STMP/INSTALL.TAG" ::/INSTALL.TAG
rm -rf "$STMP"
echo "built $SETUP (1.44 MiB setup floppy)"

# --- 4) install disk 1: system files (ATA, 8 MiB FAT16 version) ----------
dd if=/dev/zero of="$INST1" bs=1M count=8 status=none
mkfs.fat -F 16 -s 2 -n "ZBE-INST1" "$INST1" >/dev/null
SYS1="$(mktemp -d)"
mkdir -p "$SYS1/SYSTEM"
mkdir -p "$SYS1/BOOT"
mkdir -p "$SYS1/BIN"
cp "$KERNEL" "$SYS1/SYSTEM/KERNEL.BIN"
cp "$STAGE2" "$SYS1/SYSTEM/STAGE2.BIN"
cp "$STAGE1" "$SYS1/BOOT/STAGE1.BIN"
cp "$STAGE2" "$SYS1/BOOT/STAGE2.BIN"
printf "Zenbite v0.2\n" > "$SYS1/SYSTEM/ZENBITE.SYS"
printf "1" > "$SYS1/INSTALL.TAG"
cat > "$SYS1/SYSTEM/README.TXT" <<'TXT'
Zenbite v0.2 System Files

KERNEL.BIN    - Main kernel binary
STAGE2.BIN    - Second-stage bootloader
ZENBITE.SYS   - System version marker

To reinstall bootloader on a drive: see BOOT\STAGE1.BIN
TXT
cat > "$SYS1/README.TXT" <<'TXT'
Zenbite Setup Disk 1.

This disk holds the system files (kernel, loader, version tag) under
\SYSTEM\. During setup they are copied to your target drive.

Do not modify files on this disk.
TXT
# Default wallpaper image. Loaded by the desktop's "Image" wallpaper
# style. Plain ASCII -- the user can replace with any 80x24 art.
cat > "$SYS1/WALL.TXT" <<'TXT'
+----------------------------------------------------------------------------+
|                                                                            |
|                          Z   E   N   B   I   T   E                         |
|                          ---------------------                             |
|                                                                            |
|                              o   o   o   o                                 |
|                                                                            |
|                          A 32-bit retro OS                                 |
|                                                                            |
|                                                                            |
|                                                                            |
|        ......................................................             |
|        ......................................................             |
|                                                                            |
|                                                                            |
|                                                                            |
|                                                                            |
|                                                                            |
|                  Settings > Pattern > Image (WALL.TXT)                     |
|                                                                            |
|                  Edit A:\WALL.TXT to customise.                            |
|                                                                            |
|                                                                            |
+----------------------------------------------------------------------------+
TXT
mmd -i "$INST1" ::/SYSTEM
for f in "$SYS1"/SYSTEM/*; do mcopy -i "$INST1" "$f" "::/SYSTEM/$(basename "$f")"; done
mmd -i "$INST1" ::/BOOT
for f in "$SYS1"/BOOT/*; do mcopy -i "$INST1" "$f" "::/BOOT/$(basename "$f")"; done
mmd -i "$INST1" ::/BIN
mcopy -i "$INST1" "$SYS1/INSTALL.TAG" ::/INSTALL.TAG
mcopy -i "$INST1" "$SYS1/README.TXT"  ::/README.TXT
mcopy -i "$INST1" "$SYS1/WALL.TXT"    ::/WALL.TXT
rm -rf "$SYS1"
echo "built $INST1 (Setup Disk 1)"

# --- 5) install disk 2: samples ------------------------------------------
dd if=/dev/zero of="$INST2" bs=1M count=8 status=none
mkfs.fat -F 16 -s 2 -n "ZBE-INST2" "$INST2" >/dev/null
SYS2="$(mktemp -d)"
mkdir -p "$SYS2/SAMPLES"
cat > "$SYS2/SAMPLES/HELLO.C" <<'C'
// Hello world in Zenbite C.
int main() {
    puts("Hello, Zenbite!");
    return 0;
}
C
cat > "$SYS2/SAMPLES/FIB.C" <<'C'
// First 12 Fibonacci numbers.
int main() {
    int a = 0; int b = 1; int i = 0;
    while (i < 12) {
        print(a);
        int t = a + b; a = b; b = t;
        i = i + 1;
    }
    return 0;
}
C
cat > "$SYS2/SAMPLES/FIZZBUZZ.C" <<'C'
// Classic FizzBuzz.
int main() {
    int i = 1;
    while (i <= 30) {
        if (i % 15 == 0)     puts("FizzBuzz");
        else if (i % 3 == 0) puts("Fizz");
        else if (i % 5 == 0) puts("Buzz");
        else                 print(i);
        i = i + 1;
    }
    return 0;
}
C
cat > "$SYS2/SAMPLES/INDEX.TXT" <<'TXT'
Zenbite sample programs:
  HELLO.C      hello world
  FIB.C        Fibonacci numbers
  FIZZBUZZ.C   classic warmup

Try:   cc HELLO.C
TXT
printf "2" > "$SYS2/INSTALL.TAG"
mmd -i "$INST2" ::/SAMPLES
for f in "$SYS2"/SAMPLES/*; do mcopy -i "$INST2" "$f" "::/SAMPLES/$(basename "$f")"; done
mcopy -i "$INST2" "$SYS2/INSTALL.TAG" ::/INSTALL.TAG
rm -rf "$SYS2"
echo "built $INST2 (Setup Disk 2)"

# --- 6) combined install "CD": everything on one FAT16 disk -----------
# A single source disk carrying SYSTEM/, BOOT/ and SAMPLES/ together --
# the "one CD" scenario. The installer's find_source_dir() locates each
# directory wherever it lives, so this disk alone is enough to install.
CD="zenbite_install_cd.img"
dd if=/dev/zero of="$CD" bs=1M count=16 status=none
mkfs.fat -F 16 -s 2 -n "ZENBITE-CD" "$CD" >/dev/null
CDT="$(mktemp -d)"
mkdir -p "$CDT/SYSTEM" "$CDT/BOOT" "$CDT/SAMPLES"
cp "$KERNEL" "$CDT/SYSTEM/KERNEL.BIN"
cp "$STAGE2" "$CDT/SYSTEM/STAGE2.BIN"
printf 'Zenbite v0.3\n' > "$CDT/SYSTEM/ZENBITE.SYS"
cp "$STAGE1" "$CDT/BOOT/STAGE1.BIN"
cp "$STAGE2" "$CDT/BOOT/STAGE2.BIN"
cat > "$CDT/SAMPLES/HELLO.C" <<'C'
int main() { puts("Hello, Zenbite!"); return 0; }
C
printf '1' > "$CDT/INSTALL.TAG"
mmd -i "$CD" ::/SYSTEM
for f in "$CDT"/SYSTEM/*;  do mcopy -i "$CD" "$f" "::/SYSTEM/$(basename "$f")";  done
mmd -i "$CD" ::/BOOT
for f in "$CDT"/BOOT/*;    do mcopy -i "$CD" "$f" "::/BOOT/$(basename "$f")";    done
mmd -i "$CD" ::/SAMPLES
for f in "$CDT"/SAMPLES/*; do mcopy -i "$CD" "$f" "::/SAMPLES/$(basename "$f")"; done
mcopy -i "$CD" "$CDT/INSTALL.TAG" ::/INSTALL.TAG
# Make the CD itself bootable when written to a USB stick / HDD / loop
# device. Same pattern as the USB image: put the HDD variants of
# stage2 + kernel at the root, then overwrite the first sector with
# stage1_hdd while preserving the FAT16 BPB. CD-ROM El Torito booting
# would need an ISO 9660 wrapper -- this 16 MiB FAT image dd's
# directly to USB instead.
mcopy -i "$CD" "$STAGE2_HDD" ::/STAGE2.BIN
mcopy -i "$CD" "$KERNEL"     ::/KERNEL.BIN
python3 - "$CD" "$STAGE1_HDD" <<'PY'
import struct, sys
img, stage1 = sys.argv[1], sys.argv[2]
with open(img, "r+b") as f:
    bpb = bytearray(f.read(512)[3:62])
    # hidden_sectors at boot-sector offset 0x1C. CD image is not on a
    # partition (no MBR wrapper), so leave it 0.
    bpb[0x19:0x1D] = struct.pack("<I", 0)
    s1 = open(stage1, "rb").read()
    assert len(s1) == 512, len(s1)
    sector = bytearray(s1)
    sector[3:62]    = bytes(bpb)
    sector[510:512] = b"\x55\xaa"
    f.seek(0)
    f.write(sector)
PY
rm -rf "$CDT"
echo "built $CD (combined install CD, 16 MiB, bootable)"


# --- 7) USB image: partitioned, MBR-bootable --------------------------
# Modern BIOSes refuse to boot a USB stick that lacks an MBR partition
# table. We build a 32 MiB image with:
#   sector 0          : Zenbite chainloader MBR + partition table
#   sectors 2048..N   : one active FAT16 partition holding the install CD
# The chainloader reads the partition's first sector (a normal Zenbite
# FAT16 boot sector, stage1_hdd) and jumps to it.
USB="zenbite_usb.img"
MBR="$BUILD/boot/mbr.bin"
[[ -f "$MBR" ]]        || { echo "missing $MBR" >&2; exit 1; }
[[ -f "$STAGE1_HDD" ]] || { echo "missing $STAGE1_HDD" >&2; exit 1; }
[[ -f "$STAGE2_HDD" ]] || { echo "missing $STAGE2_HDD" >&2; exit 1; }

USB_SECTORS=65536              # 32 MiB
PART_START=2048                # 1 MiB alignment
PART_SECTORS=$((USB_SECTORS - PART_START))

# 1. Empty 32 MiB image.
dd if=/dev/zero of="$USB" bs=512 count=$USB_SECTORS status=none

# 2. Create the FAT16 partition image at the correct offset.
PART_IMG="$(mktemp)"
dd if=/dev/zero of="$PART_IMG" bs=512 count=$PART_SECTORS status=none
mkfs.fat -F 16 -s 4 -n "ZENBITE-USB" "$PART_IMG" >/dev/null

# 3. Populate the partition with the install content.
UTMP="$(mktemp -d)"
mkdir -p "$UTMP/SYSTEM" "$UTMP/BOOT" "$UTMP/SAMPLES"
cp "$KERNEL"  "$UTMP/SYSTEM/KERNEL.BIN"
cp "$STAGE2"  "$UTMP/SYSTEM/STAGE2.BIN"
printf 'Zenbite v0.3\n' > "$UTMP/SYSTEM/ZENBITE.SYS"
cp "$STAGE1_HDD" "$UTMP/BOOT/STAGE1.BIN"
cp "$STAGE2"     "$UTMP/BOOT/STAGE2.BIN"
cat > "$UTMP/SAMPLES/HELLO.C" <<'C'
int main() { puts("Hello, Zenbite!"); return 0; }
C
printf '1' > "$UTMP/INSTALL.TAG"
mmd -i "$PART_IMG" ::/SYSTEM
for f in "$UTMP"/SYSTEM/*;  do mcopy -i "$PART_IMG" "$f" "::/SYSTEM/$(basename "$f")";  done
mmd -i "$PART_IMG" ::/BOOT
for f in "$UTMP"/BOOT/*;    do mcopy -i "$PART_IMG" "$f" "::/BOOT/$(basename "$f")";    done
mmd -i "$PART_IMG" ::/SAMPLES
for f in "$UTMP"/SAMPLES/*; do mcopy -i "$PART_IMG" "$f" "::/SAMPLES/$(basename "$f")"; done
mcopy -i "$PART_IMG" "$UTMP/INSTALL.TAG" ::/INSTALL.TAG
# stage1_hdd loads "STAGE2  BIN" from the root directory; stage2_hdd
# then loads "KERNEL  BIN" the same way. Use the HDD/FAT16 variants
# (floppy stage2 only understands FAT12).
mcopy -i "$PART_IMG" "$STAGE2_HDD" ::/STAGE2.BIN
mcopy -i "$PART_IMG" "$KERNEL" ::/KERNEL.BIN
rm -rf "$UTMP"

# 4. Overwrite the partition's first sector with stage1_hdd while
#    preserving the FAT16 BPB that mkfs.fat just wrote. Patch the
#    BPB's hidden_sectors (offset 0x1C, 4 bytes LE) to the partition's
#    start LBA so stage1_hdd/stage2_hdd compute absolute disk LBAs
#    instead of partition-relative ones.
python3 - "$PART_IMG" "$STAGE1_HDD" "$PART_START" <<'PY'
import struct, sys
img, stage1, part_start = sys.argv[1], sys.argv[2], int(sys.argv[3])
with open(img, "r+b") as f:
    bpb = bytearray(f.read(512)[3:62])
    # hidden_sectors lives at boot-sector offset 0x1C, i.e. bpb index
    # 0x1C - 3 = 0x19 .. 0x1C.
    bpb[0x19:0x1D] = struct.pack("<I", part_start)
    s1 = open(stage1, "rb").read()
    assert len(s1) == 512, len(s1)
    sector = bytearray(s1)
    sector[3:62]    = bytes(bpb)
    sector[510:512] = b"\x55\xaa"
    f.seek(0)
    f.write(sector)
PY

# 5. Splice partition image into the USB image at PART_START.
dd if="$PART_IMG" of="$USB" bs=512 seek=$PART_START count=$PART_SECTORS \
    conv=notrunc status=none
rm -f "$PART_IMG"

# 6. Write the MBR boot code (bytes 0..445) into sector 0 of the USB.
#    Then build the partition table by hand at offset 0x1BE.
dd if="$MBR" of="$USB" bs=1 count=446 conv=notrunc status=none

python3 - "$USB" "$PART_START" "$PART_SECTORS" <<'PY'
import struct, sys
img, start, count = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
# Partition table entry, 16 bytes:
#   0x00  boot flag (0x80 = active)
#   0x01  start CHS  (head, sector/cyl_hi, cyl_lo) -- legacy, we use FE FF FF
#   0x04  type (0x0E = FAT16 LBA)
#   0x05  end CHS    (legacy) -- also FE FF FF
#   0x08  start LBA (LE u32)
#   0x0C  sector count (LE u32)
entry = bytearray(16)
entry[0]   = 0x80
entry[1:4] = b"\xFE\xFF\xFF"
entry[4]   = 0x0E
entry[5:8] = b"\xFE\xFF\xFF"
entry[8:12]  = struct.pack("<I", start)
entry[12:16] = struct.pack("<I", count)
with open(img, "r+b") as f:
    f.seek(0x1BE)
    f.write(entry)
    # Three empty partition slots.
    f.write(b"\x00" * 48)
    # Boot signature.
    f.seek(0x1FE)
    f.write(b"\x55\xAA")
PY

echo "built $USB (USB-bootable, MBR + FAT16 partition, 32 MiB)"

# --- 8) target: blank --------------------------------------------------
dd if=/dev/zero of="$TARGET" bs=1M count=64 status=none
echo "built $TARGET (blank target, 64 MiB)"
