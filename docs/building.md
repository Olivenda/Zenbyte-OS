# Building Platin Zenbyte

## Prerequisites

The build host needs:

```
bash  cpio  gzip  find  grub2-mkrescue  shellcheck (for lint)
```

The kernel binary (`build/bzImage`) must already be present. It is not built
by these targets — either build it separately or install it via:

```bash
zbpm install kernel
cp /boot/vmlinuz build/bzImage
```

## Make Targets

| Target | What it does |
|---|---|
| `make` / `make initramfs` | Pack `rootfs/` into `init.cpio` |
| `make iso` | Build `build/zenbyte.iso` (requires `init.cpio` + `build/bzImage`) |
| `make lint` | Run `shellcheck` on all shipped Bash scripts |
| `make clean` | Delete `init.cpio` and `build/zenbyte.iso` (keeps the kernel) |
| `make distclean` | Also deletes `build/bzImage` |

## Building the Initramfs

```bash
make initramfs
```

This runs:

```bash
cd rootfs && find . -mindepth 1 \( -type f -o -type l -o -type d \) -print0 \
  | LC_ALL=C sort -z \
  | cpio -o -H newc --null --quiet \
  | gzip -9 > ../init.cpio.tmp
mv init.cpio.tmp init.cpio
```

Key details:
- **Always rebuilt** — the `init.cpio` target is `.PHONY`. Enumerating rootfs
  files as Make deps fails for filenames with commas (Broadcom firmware). The
  repack takes a few seconds so unconditional rebuild is acceptable.
- **Deterministic order** — `LC_ALL=C sort -z` ensures the same cpio member
  order regardless of filesystem readdir order.
- Both regular files and symlinks are packed — symlinks must be real symlinks
  in the working tree, not plain text files.

## Building the ISO

```bash
make iso
```

Requires `build/bzImage` and `init.cpio` to exist first.

The Makefile:

1. Creates a staging tree at `build/iso-stage/`:
   ```
   build/iso-stage/
     boot/
       vmlinuz       ← copy of build/bzImage
       initramfs.img ← copy of init.cpio
       grub/
         grub.cfg    ← copy of iso/boot/grub/grub.cfg
   ```
2. Calls `grub2-mkrescue` to wrap it into a hybrid ISO (boots under both BIOS
   and UEFI):
   ```bash
   grub2-mkrescue -o build/zenbyte.iso build/iso-stage \
     --product-name=ZenbyteOS --volid=ZenbyteOS
   ```

The volume label `ZenbyteOS` is what `grub.cfg` uses to locate the root
device (`search --label ZenbyteOS`) — do not change it without updating
`iso/boot/grub/grub.cfg`.

## GRUB Configuration

`iso/boot/grub/grub.cfg` provides three live-boot entries:

| Entry | Kernel cmdline |
|---|---|
| ZenbyteOS (live) | `root=LABEL=ZenbyteOS rw quiet` |
| ZenbyteOS (live, verbose) | `root=LABEL=ZenbyteOS rw` |
| ZenbyteOS (German menu) | `root=LABEL=ZenbyteOS rw quiet lang=de` |

The `lang=de` parameter is read by `rootfs/init` to switch the boot menu and
installer to German.

## Linting

```bash
make lint
```

Runs `shellcheck -x` against:

- `rootfs/init`
- `rootfs/install.sh`
- `rootfs/usr/bin/zbpm`
- `rootfs/var/lib/zbpm/scripts/xfce4-desktop.post`
- `rootfs/var/lib/zbpm/scripts/kernel.post`

All Bash files shipped in the rootfs should pass without warnings before a
release. Add new shell scripts to `SHELL_FILES` in the Makefile.

## Testing in a VM

```bash
# Build the ISO
make iso

# Boot it (requires qemu-system-x86_64 with UEFI firmware)
qemu-system-x86_64 \
  -enable-kvm -m 2048 \
  -bios /usr/share/edk2/ovmf/OVMF_CODE.fd \
  -cdrom build/zenbyte.iso \
  -boot d

# Or BIOS mode (simpler)
qemu-system-x86_64 -enable-kvm -m 2048 -cdrom build/zenbyte.iso -boot d
```

From the boot menu, select option **2** to run the installer, or option **1**
to explore the live environment.

## Development Workflow — Iterating on rootfs

Since `make initramfs` is always fast, the cycle is:

```bash
# Edit a file in rootfs/
vim rootfs/usr/bin/zbpm

# Repack + test
make initramfs
qemu-system-x86_64 ... -kernel build/bzImage -initrd init.cpio -append "rw quiet"
```

Passing `-kernel` and `-initrd` directly to QEMU skips GRUB and boots the
initramfs straight, which is faster for iteration.
