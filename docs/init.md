# Init System

## Overview

PID 1 on Platin Zenbyte is `rootfs/init` — a plain Bash script. It handles
everything from early boot up to the point where systemd is exec'd. There is
no initrd framework (no dracut, no busybox init) in the live image; the script
is self-contained.

The decision to not use `set -e` in PID 1 is deliberate: a single mount
failure or network timeout must not kill the kernel. Each step either handles
its own error or calls `panic_shell` which drops to bash as a last resort.

## Boot Sequence

### 1. Mount pseudo-filesystems

```bash
mount -t proc     none /proc
mount -t sysfs    none /sys
mount -t devtmpfs none /dev
mount -t devpts   devpts /dev/pts
mount -t tmpfs    tmpfs /run
```

Each mount is attempted twice (with different source arguments) before emitting
a warning. The boot continues even if a mount fails — `/dev/pts` and `/run` are
non-fatal.

### 2. Network

The script iterates `/sys/class/net/*` to find the first non-loopback
interface, brings it up with `ip link set <iface> up`, and attempts DHCP:

- Tries `dhclient` first, then `dhcpcd` as fallback.
- Each attempt has a **10-second timeout** (`timeout 10 dhclient -1 <iface>`).
- Only the first successful interface is used; DHCP runs synchronously so the
  menu does not appear until the network attempt completes (or times out).
- `NET_OK` is set to `1` on success and shown in the boot menu.

### 3. Language detection

```bash
read -r _cmdline < /proc/cmdline
case "$_cmdline" in
  *lang=de*) MENU_LANG="de" ;;
  *lang=en*) MENU_LANG="en" ;;
esac
```

The default is English. Add `lang=de` to the kernel command line (the GRUB
entry "ZenbyteOS (German menu)" does this) to switch to German.

### 4. Boot menu

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

The menu loops until a valid choice is made:

| Choice | Action |
|---|---|
| `1` or Enter | Break out of the loop → proceed to systemd |
| `2` | Execute `/install.sh`; on return, prompt to reboot or drop to shell |
| `3` | Prompt for confirmation word `emergency`, then `exec /bin/bash` |
| anything else | Print "invalid choice", sleep 1, redraw menu |

### 5. Reboot after install

The installer has no systemd available, so the init script uses the kernel
sysrq interface to reboot:

```bash
sync
echo b > /proc/sysrq-trigger 2>/dev/null
reboot -f
```

### 6. Exec systemd

```bash
exec /usr/lib/systemd/systemd
```

If the binary is not present or not executable, `panic_shell` is called and
the kernel gets an emergency bash session instead of hanging.

## Emergency Shell

Two paths lead to an emergency shell:

1. **Boot menu option 3** — user types `emergency` to confirm.
2. **`panic_shell`** — called if systemd is missing. Tries `exec /bin/bash`
   first, then `exec /bin/sh`.

From the emergency shell you have full access to the live rootfs. Useful for
diagnosing mount failures, testing package installs, or debugging the
installer.

## Modifying init

`rootfs/init` is a regular tracked file. After editing:

```bash
# Repack the initramfs
make initramfs

# Lint before committing
make lint
```

`shellcheck` is run against it as part of `make lint`. All variables should be
quoted and no command output should be parsed with `ls`.

## Key Design Decisions

**No `set -e` in PID 1.** A single failed mount or missing command would kill
the process and panic the kernel with no useful output. Every critical path has
explicit error handling via `panic_shell`.

**Synchronous DHCP.** The network attempt blocks for up to 10 seconds before
the menu appears. This avoids a race where the user selects "Install" before a
DHCP lease is acquired and the installer cannot reach the zbpm mirror.

**`exec systemd` not `systemctl`.** Replacing PID 1 via `exec` is the correct
way to hand off — there is no intermediate process left alive. systemd becomes
PID 1.
