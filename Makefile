# Build orchestration for ZenbyteOS.
#
# Targets:
#   make initramfs   — pack rootfs/ into init.cpio at the repo root
#   make iso         — assemble a bootable ISO under build/zenbyte.iso
#   make lint        — shellcheck all Bash entry points
#   make clean       — drop generated artifacts (keeps the kernel)
#   make distclean   — also drop the kernel binary

ROOTFS      := rootfs
BUILD_DIR   := build
ISO_DIR     := iso
INITRAMFS   := init.cpio
KERNEL      := $(BUILD_DIR)/bzImage
ISO         := $(BUILD_DIR)/zenbyte.iso
SHELL_FILES := $(ROOTFS)/init $(ROOTFS)/install.sh $(ROOTFS)/usr/bin/zbpm \
               $(ROOTFS)/var/lib/zbpm/scripts/xfce4-desktop.post \
               $(ROOTFS)/var/lib/zbpm/scripts/kernel.post

# grub2-mkrescue on Fedora/RHEL; grub-mkrescue on Ubuntu/Debian
GRUB_MKRESCUE := $(shell command -v grub2-mkrescue 2>/dev/null || command -v grub-mkrescue 2>/dev/null)

.PHONY: all initramfs iso lint clean distclean help $(INITRAMFS)

all: initramfs

help:
	@grep -E '^[a-zA-Z_-]+:.*?##' $(MAKEFILE_LIST) | \
	  awk 'BEGIN{FS=":.*?## "}{printf "  %-12s %s\n", $$1, $$2}'

initramfs: $(INITRAMFS) ## Pack rootfs/ into init.cpio (gzip-compressed cpio)

# init.cpio is .PHONY — always repacked. Enumerating rootfs files as Make
# dependencies fails for paths with commas/spaces (e.g. Broadcom firmware).
# Repacking is fast (a few seconds) so unconditional rebuild is fine.
$(INITRAMFS):
	@# Seed /dev nodes that git cannot store as character devices.
	@# Silently skipped if mknod is unavailable or not run as root.
	@install -d $(ROOTFS)/dev
	@[ -c $(ROOTFS)/dev/console ] || mknod -m 600 $(ROOTFS)/dev/console c 5 1 2>/dev/null || true
	@[ -c $(ROOTFS)/dev/null ]    || mknod -m 666 $(ROOTFS)/dev/null    c 1 3 2>/dev/null || true
	@[ -c $(ROOTFS)/dev/zero ]    || mknod -m 666 $(ROOTFS)/dev/zero    c 1 5 2>/dev/null || true
	@echo "==> packing $(ROOTFS) -> $@"
	cd $(ROOTFS) && find . -mindepth 1 \( -type f -o -type l -o -type d \) -print0 | \
	  LC_ALL=C sort -z | cpio -o -H newc --null --quiet | gzip -9 > ../$(INITRAMFS).tmp
	mv $(INITRAMFS).tmp $(INITRAMFS)
	@echo "==> $@: $$(du -h $@ | cut -f1)"

iso: $(ISO) ## Build a bootable ISO

$(ISO): $(KERNEL) $(INITRAMFS) $(ISO_DIR)/boot/grub/grub.cfg
	@test -n "$(GRUB_MKRESCUE)" || \
	  { echo "grub-mkrescue / grub2-mkrescue not found. On Ubuntu: sudo apt install grub2-common grub-pc-bin grub-efi-amd64-bin xorriso mtools"; exit 1; }
	@echo "==> staging ISO tree"
	rm -rf $(BUILD_DIR)/iso-stage
	mkdir -p $(BUILD_DIR)/iso-stage/boot/grub
	cp $(KERNEL)   $(BUILD_DIR)/iso-stage/boot/vmlinuz
	cp $(INITRAMFS) $(BUILD_DIR)/iso-stage/boot/initramfs.img
	cp $(ISO_DIR)/boot/grub/grub.cfg $(BUILD_DIR)/iso-stage/boot/grub/grub.cfg
	@echo "==> $(GRUB_MKRESCUE) -> $@"
	$(GRUB_MKRESCUE) -o $@ $(BUILD_DIR)/iso-stage \
	  --product-name=ZenbyteOS -- -volid ZenbyteOS

lint: ## shellcheck all shipped Bash scripts
	@command -v shellcheck >/dev/null 2>&1 || \
	  { echo "shellcheck not installed"; exit 1; }
	shellcheck -x $(SHELL_FILES)

clean: ## Remove generated artifacts (keeps build/bzImage)
	rm -f $(INITRAMFS) $(INITRAMFS).tmp $(ISO)
	rm -rf $(BUILD_DIR)/iso-stage

distclean: clean ## Also remove the compiled kernel
	rm -f $(KERNEL)
