cd ~/Dokumente/Zenbyte-OS/rootfs
sudo find . | sudo cpio -o -H newc | gzip > ~/Dokumente/Zenbyte-OS/iso/boot/initramfs.img

grub2-mkrescue -o ~/Dokumente/Zenbyte-OS/ZenbyteOS-1.0.iso \
  ~/Dokumente/Zenbyte-OS/iso


