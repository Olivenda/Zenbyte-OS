#!/bin/bash

echo "================================"
echo "   ZenbyteOS 1.0 Installer"
echo "================================"
echo ""

echo "Verfügbare Disks:"
lsblk -d -o NAME,SIZE,TYPE | grep disk
echo ""

read -p "Ziel-Disk (z.B. sda, vda): " DISK
DISK="/dev/$DISK"

echo ""
echo "WARNUNG: Alle Daten auf $DISK werden gelöscht!"
read -p "Fortfahren? (ja/nein): " CONFIRM

if [ "$CONFIRM" != "ja" ]; then
  echo "Abgebrochen."
  exit 1
fi

echo "[1/7] Partitioniere $DISK..."
parted -s $DISK mklabel msdos
parted -s $DISK mkpart primary ext4 1MiB 100%
parted -s $DISK set 1 boot on

PART="${DISK}1"
sleep 1

echo "[2/7] Formatiere $PART..."
mkfs.ext4 -F $PART

echo "[3/7] Mounte $PART..."
mkdir -p /mnt/zenbyte
mount $PART /mnt/zenbyte

echo "[4/7] Kopiere System..."
cp -ax / /mnt/zenbyte/ 2>/dev/null

echo "[5/7] Lade Kernel via zbpm..."
mkdir -p /mnt/zenbyte/boot
REPO="http://192.168.178.71:8080/pkgs"
curl -fsSL "$REPO/kernel.tar.gz" -o /tmp/kernel.tar.gz || {
  echo "FEHLER: Kernel konnte nicht geladen werden."
  exit 1
}
tar -xzf /tmp/kernel.tar.gz -C /mnt/zenbyte/
cp /mnt/zenbyte/boot/vmlinuz-7.0.0-zenbyte /mnt/zenbyte/boot/vmlinuz

echo "[6/7] Installiere GRUB..."
grub2-install --target=i386-pc \
  --boot-directory=/mnt/zenbyte/boot \
  --force $DISK

cat > /mnt/zenbyte/boot/grub2/grub.cfg << GRUBEOF
set timeout=5
set default=0

menuentry "ZenbyteOS 1.0" {
    linux /boot/vmlinuz root=$PART rw quiet rdinit=/init
    initrd /boot/initramfs.img
}
GRUBEOF

echo "[7/7] Initramfs kopieren..."
cp /boot/initramfs.img /mnt/zenbyte/boot/initramfs.img

umount /mnt/zenbyte
echo ""
echo "Installation abgeschlossen!"
echo "Starte neu und entferne das ISO."
