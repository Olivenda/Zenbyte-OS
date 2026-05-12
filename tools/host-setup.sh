#!/usr/bin/env bash
# host-setup.sh — install build dependencies for ZenbyteOS on Fedora or Ubuntu.
#
# Usage:
#   sudo tools/host-setup.sh
#
# What it does:
#   Fedora/RHEL  — installs packages via dnf (already has rpm toolchain)
#   Ubuntu/Debian — installs packages via apt, then adds Fedora 41 repos so
#                   that `dnf download` can fetch Fedora RPMs for packaging.
#
# After running this script the full toolchain works identically on both hosts:
#   tools/zbpm-keygen, tools/zbpm-build, tools/build-kernel-pkg, make iso, etc.

set -euo pipefail

FEDORA_RELEASE="${FEDORA_RELEASE:-41}"
FEDORA_ARCH="${FEDORA_ARCH:-x86_64}"

die()  { echo "error: $*" >&2; exit 1; }
info() { echo "==> $*"; }

[ "$(id -u)" -eq 0 ] || die "run this script as root: sudo $0"

# ── detect host ────────────────────────────────────────────────────────────
if [ -f /etc/os-release ]; then
  # shellcheck source=/dev/null
  . /etc/os-release
  HOST_ID="${ID:-unknown}"
  HOST_ID_LIKE="${ID_LIKE:-}"
else
  HOST_ID="unknown"
  HOST_ID_LIKE=""
fi

is_fedora() {
  [[ "$HOST_ID" == "fedora" || "$HOST_ID" == "rhel" || \
     "$HOST_ID" == "centos" || "$HOST_ID_LIKE" == *"fedora"* || \
     "$HOST_ID_LIKE" == *"rhel"* ]]
}

is_ubuntu() {
  [[ "$HOST_ID" == "ubuntu" || "$HOST_ID" == "debian" || \
     "$HOST_ID_LIKE" == *"debian"* ]]
}

# ── Fedora / RHEL host ─────────────────────────────────────────────────────
if is_fedora; then
  info "Fedora/RHEL host — installing build dependencies via dnf"
  dnf install -y \
    grub2-tools grub2-efi-x64-modules \
    xorriso cpio gzip \
    rpm rpm-build rpm2cpio dnf \
    gpg gpgv \
    shellcheck \
    python3 \
    flock \
    make
  info "All done — Fedora host is ready."
  exit 0
fi

# ── Ubuntu / Debian host ───────────────────────────────────────────────────
if is_ubuntu; then
  info "Ubuntu/Debian host — installing build dependencies via apt"

  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq

  apt-get install -y \
    xorriso cpio gzip \
    rpm rpm2cpio \
    gnupg gpgv \
    shellcheck \
    python3 \
    util-linux \
    make \
    curl \
    grub2-common \
    grub-efi-amd64-bin \
    grub-pc-bin \
    mtools \
    dnf

  # ── Configure Fedora repos so `dnf download` fetches Fedora RPMs ──────
  info "Configuring Fedora ${FEDORA_RELEASE} repos for dnf (needed by zbpm-build)"

  mkdir -p /etc/yum.repos.d

  cat > /etc/yum.repos.d/fedora.repo << EOF
[fedora]
name=Fedora ${FEDORA_RELEASE} - ${FEDORA_ARCH}
baseurl=https://dl.fedoraproject.org/pub/fedora/linux/releases/${FEDORA_RELEASE}/Everything/${FEDORA_ARCH}/os/
enabled=1
gpgcheck=1
gpgkey=https://dl.fedoraproject.org/pub/fedora/linux/releases/${FEDORA_RELEASE}/Everything/${FEDORA_ARCH}/os/repodata/repomd.xml.key
metadata_expire=7d
EOF

  cat > /etc/yum.repos.d/fedora-updates.repo << EOF
[fedora-updates]
name=Fedora ${FEDORA_RELEASE} Updates - ${FEDORA_ARCH}
baseurl=https://dl.fedoraproject.org/pub/fedora/linux/updates/${FEDORA_RELEASE}/Everything/${FEDORA_ARCH}/
enabled=1
gpgcheck=1
gpgkey=https://dl.fedoraproject.org/pub/fedora/linux/updates/${FEDORA_RELEASE}/Everything/${FEDORA_ARCH}/repodata/repomd.xml.key
metadata_expire=6h
EOF

  # Fetch Fedora GPG keys so dnf gpgcheck passes
  info "Importing Fedora GPG key"
  curl -fsSL \
    "https://dl.fedoraproject.org/pub/fedora/linux/releases/${FEDORA_RELEASE}/Everything/${FEDORA_ARCH}/os/RPM-GPG-KEY-fedora-${FEDORA_RELEASE}-primary" \
    -o "/etc/pki/rpm-gpg/RPM-GPG-KEY-fedora-${FEDORA_RELEASE}-primary" 2>/dev/null || {
      # Fallback: disable gpgcheck if key fetch fails
      info "Warning: could not fetch Fedora GPG key — disabling gpgcheck (dev mode)"
      sed -i 's/gpgcheck=1/gpgcheck=0/' /etc/yum.repos.d/fedora.repo \
                                         /etc/yum.repos.d/fedora-updates.repo
  }

  # Quick smoke-test: dnf repolist should show fedora repos
  info "Verifying dnf can see Fedora repos..."
  dnf repolist 2>/dev/null | grep -q "fedora" \
    && info "Fedora repos active ✓" \
    || info "Warning: fedora repo not listed — check /etc/yum.repos.d/"

  info ""
  info "Ubuntu host is ready. You can now run:"
  info "  tools/zbpm-build --out-dir build/pkgs nano"
  info "  make -f tools/build-all.mk REPO=./repo all"
  exit 0
fi

# ── Unknown host ───────────────────────────────────────────────────────────
die "Unsupported host OS: $HOST_ID. Manually install: dnf rpm rpm2cpio cpio gzip gpg gpgv xorriso shellcheck python3"
