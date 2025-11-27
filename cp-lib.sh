#!/bin/bash
# Usage: ./copy_libs.sh /path/to/binary
# Copies all shared libs needed by the binary into your rootfs

BIN="$1"
ROOTFS="$HOME/Zenbyte-OS/initfs"

if [ -z "$BIN" ]; then
    echo "Usage: $0 /path/to/binary"
    exit 1
fi

echo "[*] Analyzing libraries for $BIN..."
ldd "$BIN" | grep "=> /" | awk '{print $3}' | while read LIB; do
    if [ -f "$LIB" ]; then
        echo "[*] Copying $LIB"
        # Decide target directory
        case "$LIB" in
            /lib64/*) DEST="$ROOTFS/lib64" ;;
            /lib/*)   DEST="$ROOTFS/lib" ;;
            /usr/lib/*) DEST="$ROOTFS/usr/lib" ;;
            *) DEST="$ROOTFS/lib" ;;  # fallback
        esac
        mkdir -p "$DEST"
        cp -v --preserve=links "$LIB" "$DEST/"
    fi
done

echo "[*] Done!"
