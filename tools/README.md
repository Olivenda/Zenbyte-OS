# tools/ — zbpm repository build & sign tooling

This directory holds the **publisher** half of zbpm: the scripts that turn
RPMs (or any tarball) into a signed, browsable repository that
`/usr/bin/zbpm` can install from.

It is independent of the live system: nothing here writes outside of this
directory tree (signing key, staging) and the `REPO=` directory you
choose.

| File              | Purpose                                                                |
|-------------------|------------------------------------------------------------------------|
| `zbpm-keygen`     | Generate the RSA-4096 GPG signing key in a *dedicated* GnuPG home.     |
| `zbpm-build`      | (companion tool) Convert one dnf-installed RPM into a staging dir.     |
| `zbpm-repo`       | Manage the on-disk repository: init / add / remove / sign / verify.    |
| `build-all.mk`    | Drive `zbpm-build` + `zbpm-repo add` for every entry in `packages.list`.|
| `packages.list`   | The starter manifest of packages to publish.                           |
| `keys/`           | Created by `zbpm-keygen`. Contains the secret key — back this up.      |

## One-time setup

1. Generate the signing key (interactive, or pass `--name` / `--email`):

   ```sh
   tools/zbpm-keygen --name "ZenbyteOS Releases" --email releases@zenbyte.example
   ```

   This creates:

   - `tools/keys/.gnupg/`     — the secret keyring (mode 0700, do **not** commit)
   - `tools/keys/zbpm-pub.asc` — the armored public key (safe to commit / publish)
   - `tools/keys/zbpm.gpg`     — a binary keyring suitable for `gpgv`

2. Install the public keyring into the rootfs so target systems trust it:

   ```sh
   sudo install -d -m 0700 rootfs/etc/zbpm/keys
   sudo install -m 0644 tools/keys/zbpm.gpg rootfs/etc/zbpm/keys/zbpm.gpg
   ```

   `zbpm` looks for `/etc/zbpm/keys/zbpm.gpg` (or `zbpm.kbx`) and refuses
   to install anything not signed by a key in that file when
   `ZBPM_REQUIRE_SIGNATURE=1` (the default).

## Per-build workflow

1. Edit `tools/packages.list`. Each non-comment line is either:

   ```
   <dnf-pkg-name>
   <dnf-pkg-name>   <zbpm-pkg-name>     # rename for the zbpm repo
   ```

2. Build and ingest everything in parallel:

   ```sh
   make -j8 -f tools/build-all.mk REPO=./repo all
   ```

   - `tools/zbpm-build` produces `staging/<pkg>/<pkg>.{tar.gz,deps,info,version}`.
   - `tools/zbpm-repo add` copies the artefacts into `./repo/`, generates
     `<pkg>.tar.gz.sha256`, signs `<pkg>.tar.gz.sig`, and rebuilds
     `index.txt`.
   - The `add` step is serialised through `flock` on `./repo/.lock`,
     so `-jN` is safe.

3. Verify the result locally (this runs the exact same checks as zbpm
   does on a target):

   ```sh
   tools/zbpm-repo verify ./repo
   ```

4. Serve it for a test VM:

   ```sh
   tools/zbpm-repo serve ./repo 8080
   ```

   On the test VM, edit `/etc/zbpm/mirrors`:

   ```
   http://<host-ip>:8080
   ```

   Then `zbpm sync && zbpm install <pkg>`.

## Other useful subcommands

```sh
tools/zbpm-repo init        ./repo                # bootstrap empty repo
tools/zbpm-repo add         ./repo staging/bash   # ingest one package
tools/zbpm-repo remove      ./repo bash           # delete + rebuild index
tools/zbpm-repo regen-index ./repo                # rebuild index.txt
tools/zbpm-repo sign-all    ./repo                # re-sign anything stale
```

`zbpm-repo add` is **idempotent** — running it twice on the same staging
directory produces the same files. The signature file changes byte-for-byte
each invocation (GnuPG includes a timestamp), but it still verifies
identically against the same keyring.

## Constraints honored

- `set -euo pipefail` everywhere.
- No `eval` on untrusted input.
- All package names validated against `^[A-Za-z0-9_+][A-Za-z0-9_.+-]{0,63}$`
  (same regex as `rootfs/usr/bin/zbpm`).
- No external deps beyond `bash`, `gpg`, `gpgv`, `sha256sum`, `tar`,
  `gzip`, `awk`, `sed`, plus `python3` (only for `zbpm-repo serve`).
- Nothing under this directory ever writes to the user's `~/.gnupg`.

## Recovery & secrets

`tools/keys/.gnupg/` holds the **only** copy of the secret signing key.
Losing it means losing the ability to publish updates that existing
installations will accept (they would have to install a new public
keyring first). Back it up to offline storage and add it to
`.gitignore` if you have not already.
