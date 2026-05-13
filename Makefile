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
	@echo "==> packing $(ROOTFS) -> $@"
	cd $(ROOTFS) && find . -mindepth 1 \( -type f -o -type l -o -type d \) -print0 | \
	  LC_ALL=C sort -z | cpio -o -H newc --null --quiet | gzip -9 > ../$(INITRAMFS).tmp
	mv $(INITRAMFS).tmp $(INITRAMFS)
	@echo "==> $@: $$(du -h $@ | cut -f1)"

iso: $(ISO) ## Build a bootable ISO

$(ISO): $(KERNEL) $(INITRAMFS) $(ISO_DIR)/boot/grub/grub.cfg
	@command -v grub2-mkrescue >/dev/null 2>&1 || \
	  { echo "grub2-mkrescue is required for 'make iso'"; exit 1; }
	@echo "==> staging ISO tree"
	rm -rf $(BUILD_DIR)/iso-stage
	mkdir -p $(BUILD_DIR)/iso-stage/boot/grub
	cp $(KERNEL)   $(BUILD_DIR)/iso-stage/boot/vmlinuz
	cp $(INITRAMFS) $(BUILD_DIR)/iso-stage/boot/initramfs.img
	cp $(ISO_DIR)/boot/grub/grub.cfg $(BUILD_DIR)/iso-stage/boot/grub/grub.cfg
	@echo "==> grub2-mkrescue -> $@"
	grub2-mkrescue -o $@ $(BUILD_DIR)/iso-stage \
	  --product-name=ZenbyteOS --volid=ZenbyteOS

lint: ## shellcheck all shipped Bash scripts
	@command -v shellcheck >/dev/null 2>&1 || \
	  { echo "shellcheck not installed"; exit 1; }
	shellcheck -x $(SHELL_FILES)

clean: ## Remove generated artifacts (keeps build/bzImage)
	rm -f $(INITRAMFS) $(INITRAMFS).tmp $(ISO)
	rm -rf $(BUILD_DIR)/iso-stage

distclean: clean ## Also remove the compiled kernel
	rm -f $(KERNEL)
