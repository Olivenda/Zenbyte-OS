# zbpm package repository schema

This document describes the on-disk / on-server layout that `zbpm` (v2.x) expects from a repository.

A repository is just a static directory served over HTTPS. There is no
dynamic API.

## Layout

```
<repo-root>/
  index.txt
  <pkg>.tar.gz
  <pkg>.tar.gz.sha256
  <pkg>.tar.gz.sig
  <pkg>.deps          (optional)
  <pkg>.info          (optional)
```

### `index.txt`

UTF-8, one package per line, two whitespace-separated fields:

```
<pkg-name>  <version>
```

- `pkg-name` must match `^[A-Za-z0-9_+][A-Za-z0-9_.+-]{0,63}$`
- `version` is opaque to zbpm; equality is the only operation performed
- Lines beginning with `#` are not currently parsed but are reserved

### `<pkg>.tar.gz`

A gzip-compressed tarball whose members are **relative paths** that, when
prefixed with `/`, fall inside the prefix allow-list configured in
`/etc/zbpm/zbpm.conf` (default: `/usr/`, `/etc/`, `/opt/`,
`/var/lib/zbpm/scripts/`).

Members **must not**:
- begin with `/`
- contain `..` segments
- be device nodes, sockets, or fifos

zbpm extracts to a staging directory first, validates every member, then
copies the tree into place. Any violation aborts the install with no
files written.

#### Optional in-tarball metadata

If a tarball contains an executable file at `META/postinst`, zbpm will
install it to `/var/lib/zbpm/scripts/<pkg>.post` (mode `0700`, owned by
root) and run it after extraction. This is the only sanctioned way to
ship a post-install hook — pre-existing files in `/var/lib/zbpm/scripts/`
that did not arrive through a signed package are not protected.

### `<pkg>.tar.gz.sha256`

`sha256sum` output. zbpm only reads the first whitespace-delimited field.
Required when `ZBPM_REQUIRE_SIGNATURE=1` (the default).

### `<pkg>.tar.gz.sig`

A detached GPG signature over `<pkg>.tar.gz`, verifiable by `gpgv` against
the keyring at `/etc/zbpm/keys/zbpm.gpg` (or `zbpm.kbx`). Required when
`ZBPM_REQUIRE_SIGNATURE=1`.

To produce one:

```sh
gpg --detach-sign --armor --output mypkg.tar.gz.sig mypkg.tar.gz
```

### `<pkg>.deps` (optional)

Whitespace-separated list of dependency package names. May contain
multiple lines; everything after a `#` on a line is ignored. Empty file
or 404 means "no dependencies".

### `<pkg>.info` (optional)

Free-form UTF-8 text. Shown verbatim by `zbpm info <pkg>`. Conventional
content includes a one-line summary, maintainer, homepage, and
description.

## Client-side state

zbpm writes only under these paths:

| Path                                | Purpose                                |
|-------------------------------------|----------------------------------------|
| `/var/lib/zbpm/installed/<pkg>`     | One file per installed package, content is the version string |
| `/var/lib/zbpm/installed/<pkg>.partial` | Sentinel for an interrupted install (cleared by a successful retry) |
| `/var/lib/zbpm/manifests/<pkg>`     | Newline-separated list of absolute paths owned by the package |
| `/var/lib/zbpm/scripts/<pkg>.post`  | Post-install hook (mode 0700)          |
| `/var/lib/zbpm/holds/<pkg>`         | Empty marker — package will be skipped by `upgrade` |
| `/var/cache/zbpm/`                  | Mirror index cache and per-install staging dirs |
| `/var/log/zbpm.log`                 | Append-only operation log              |

## Building a package

Minimal recipe for a hypothetical `hello` package:

```sh
mkdir -p hello/usr/bin
install -m 0755 hello.bin hello/usr/bin/hello
( cd hello && tar -czf ../hello.tar.gz . )

sha256sum hello.tar.gz > hello.tar.gz.sha256
gpg --detach-sign --armor --output hello.tar.gz.sig hello.tar.gz

echo "hello 1.0.0" >> index.txt
```

Place the four files (`index.txt` updated in place, plus `hello.tar.gz`,
`hello.tar.gz.sha256`, `hello.tar.gz.sig`) under the repository root.
That is the entire publish step.
