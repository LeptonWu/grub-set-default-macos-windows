# Dependency source

Both builds use the upstream e2fsprogs 1.47.4 source archive. The source archive
and extracted/generated build trees are not tracked in this repository.

Both build scripts download the archive automatically if the extracted source
and archive are missing, then verify the SHA-256 before extraction. Downloads
require curl. Existing extracted sources are reused without network access.
A failed or incomplete download is removed; an existing archive with a wrong
checksum is rejected rather than overwritten.

To download manually instead, run from the repository root:

```sh
curl -fL https://www.kernel.org/pub/linux/kernel/people/tytso/e2fsprogs/v1.47.4/e2fsprogs-1.47.4.tar.gz \
  -o ../e2fsprogs-1.47.4.tar.gz
```

Expected SHA-256:

```text
da274408bebbfd13a5a2fc3cfc66e3ffff17c48534673aa67f88d49b99123b96
```

Verify with `sha256sum ../e2fsprogs-1.47.4.tar.gz` on Linux, or
`shasum -a 256 ../e2fsprogs-1.47.4.tar.gz` on macOS.

Then run `./build-windows.sh` on Linux or `./build-macos.sh` on macOS.
Each script also accepts an explicit archive path as its first argument.
The build scripts extract into build/. An explicit archive path is also used
as the download destination if that file is missing.

Windows cross builds require MinGW-w64 (`gcc-mingw-w64-x86-64` on Debian).
macOS builds require Apple Command Line Tools (`xcode-select --install`).
See README.md for complete usage and validation limitations. Upstream license
notices and corresponding source remain in the extracted e2fsprogs tree.
