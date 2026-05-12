# Building zbpm packages

This guide covers turning Fedora-derived RPMs (downloaded via `dnf`) into
signed `zbpm` packages and publishing them through a local repository.

The toolchain lives under `tools/` and was designed to run on a Fedora-like
host (or any Linux box with `dnf`, `rpm2cpio`, `cpio`, `gpg`, and `gpgv`).

## Quick path

```bash
# 1. One-time: generate a repository signing key.
tools/zbpm-keygen --name "ZenbyteOS Repo" --email repo@zenbyte.os

# 2. Build a single package.
tools/zbpm-build --out-dir build/pkgs nano

# 3. Build the kernel as a single zbpm package (special-cased multi-RPM merge).
tools/build-kernel-pkg --out-dir build/pkgs

# 4. Init a local repo and ingest the staged packages.
tools/zbpm-repo init ./repo
tools/zbpm-repo add  ./repo build/pkgs/nano
tools/zbpm-repo add  ./repo build/pkgs/kernel

# 5. Verify everything.
tools/zbpm-repo verify ./repo

# 6. Serve it for a test VM.
tools/zbpm-repo serve ./repo 8765
# In the test VM: echo http://<host>:8765 > /etc/zbpm/mirrors

# 7. Install the public keyring on the test VM.
install -d -m 0700 /etc/zbpm/keys
install -m 0644 tools/keys/zbpm.gpg /etc/zbpm/keys/zbpm.gpg

# 8. Install.
zbpm install -y nano
```

## Mass-build from a manifest

`tools/packages.list` enumerates packages to build (one per line; optional
second column renames the dnf name to a zbpm name):

```
bash
nano
xfce4-session   xfce4-desktop
```

Then:

```bash
make -f tools/build-all.mk REPO=./repo all
```

The Makefile parallelises `zbpm-build` (which is independent per package)
and serialises `zbpm-repo add` via `flock` on `repo/.lock`.

## How `zbpm-build` works

For each package name:

1. `dnf download --resolve=false <pkg>` fetches the binary RPM into a
   throwaway tempdir.
2. `rpm2cpio <pkg>.rpm | cpio -idmu` extracts into a staging root.
3. Heavy paths (`/usr/share/doc`, `/usr/share/man`, locales, etc.) are
   pruned by default — override with `--strip-paths`.
4. `/lib` and `/lib64` (real dirs) are folded into `/usr/lib` and
   `/usr/lib64` since zbpm's allow-list rejects bare `/lib`.
5. Every staged path is validated against the same prefix allow-list
   that `zbpm` itself enforces at install time. If anything escapes,
   the build aborts with a clear error rather than producing a tarball
   the manager will reject.
6. RPM `Requires:` are filtered through `tools/dep-map.txt` to map
   Fedora capability names to zbpm package names. Sonames, `rpmlib(...)`,
   `config(...)`, and absolute-path requirements are dropped.
7. The RPM `%post` scriptlet (if any) is translated to
   `META/postinst` and shipped inside the tarball — zbpm extracts it
   to `/var/lib/zbpm/scripts/<pkg>.post` (mode `0700`) at install time.
8. A reproducible-ish gzip tarball is written: deterministic file
   order (`LC_ALL=C sort`), zeroed owner/group, fixed mtime
   (`SOURCE_DATE_EPOCH` honoured, default `1700000000`).

Output (consumed by `zbpm-repo add`):

```
build/pkgs/<name>/
  <name>.tar.gz
  <name>.deps
  <name>.info
  <name>.version
```

## How `build-kernel-pkg` differs

The Linux kernel ships across several RPMs (`kernel-core`,
`kernel-modules`, `kernel-modules-core`, `kernel-modules-extra`).
`build-kernel-pkg` downloads all of them, extracts into a single root,
strips the build/source trees (saving ~1 GB), and uses our own
`rootfs/var/lib/zbpm/scripts/kernel.post` as the `META/postinst` (which
re-points `/boot/vmlinuz` and regenerates the initramfs via dracut).

The resulting zbpm package is installed identically to any other:

```bash
zbpm install -y kernel
# postinst runs dracut, repoints /boot/vmlinuz, prompts for reboot
```

## How `zbpm-repo` signs

`zbpm-repo add`:

1. Copies `<name>.tar.gz`, `<name>.deps`, `<name>.info` into the repo root.
2. Generates `<name>.tar.gz.sha256` (`sha256sum` format).
3. Detached-signs the tarball: `gpg --detach-sign` against the keyring
   under `tools/keys/.gnupg`. The resulting `.sig` is verifiable by
   `gpgv --keyring /etc/zbpm/keys/zbpm.gpg`.
4. Calls `regen-index`, which rebuilds `index.txt` from per-package
   `<name>.version` sidecars.

`zbpm-repo verify` runs the exact verification logic that `zbpm` itself
applies: `sha256sum -c` plus `gpgv --keyring tools/keys/zbpm.gpg`. If
this passes locally, every package is guaranteed to install on a target.

## Local development without signatures

For iterating on the package format itself you can disable signature
enforcement on the *client*:

```bash
zbpm --no-verify install ...
# or persistently:
echo "ZBPM_REQUIRE_SIGNATURE=0" >> /etc/zbpm/zbpm.conf
```

Do **not** ship a target rootfs with signature verification disabled —
it nullifies the entire trust chain.

## Testing the pipeline without dnf

`tools/build-test-pkg` produces a hand-crafted "hello" package that
matches the `zbpm-build` output contract. Useful for exercising
`zbpm-repo` on machines that don't have `dnf` (developer laptops,
WSL/MSYS environments, CI):

```bash
tools/build-test-pkg --out-dir build/pkgs
tools/zbpm-repo add ./repo build/pkgs/hello
```

## Reference

- Wire format: [docs/package-repo-schema.md](package-repo-schema.md)
- zbpm CLI: `zbpm help`
- Configuration: `/etc/zbpm/zbpm.conf`, `/etc/zbpm/mirrors`
