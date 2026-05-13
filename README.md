# Platin Zenbyte OS

Platin Zenbyte is a lightweight, experimental Linux distribution built for
simplicity, performance, and full transparency. Every layer — boot, init,
installer, package manager — is written and maintained in this repository.

The OS is assembled from existing binary packages (Fedora RPMs, repackaged
through a custom toolchain) but the system layer is entirely our own.

## Documentation

| Document | Description |
|---|---|
| [docs/overview.md](docs/overview.md) | Architecture, component overview, boot flow |
| [docs/building.md](docs/building.md) | Build the initramfs and ISO with `make` |
| [docs/installation.md](docs/installation.md) | Install from ISO — step-by-step guide |
| [docs/init.md](docs/init.md) | How the custom PID 1 init script works |
| [docs/building-packages.md](docs/building-packages.md) | Build and publish zbpm packages |
| [docs/package-repo-schema.md](docs/package-repo-schema.md) | zbpm repository wire format |
| [tools/README.md](tools/README.md) | Host-side zbpm toolchain reference |

## Quick Start

```bash
# Build the initramfs
make initramfs

# Build a bootable ISO (requires build/bzImage)
make iso

# Write to USB and boot
dd if=build/zenbyte.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

From the boot menu, select **2** to install or **1** to explore the live
environment. See [docs/installation.md](docs/installation.md) for the full
walkthrough.

## Repository Layout

```
rootfs/       Live filesystem (packed into init.cpio)
iso/          GRUB config for the bootable ISO
build/        Generated artifacts (bzImage, zenbyte.iso)
tools/        Package build & publish toolchain
docs/         Documentation
Makefile      initramfs / ISO / lint / clean targets
```

## Status

Early development. The boot, init, installer, and package manager are
functional. The kernel and desktop packages are managed separately via zbpm.
