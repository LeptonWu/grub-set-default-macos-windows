# 这一段不是AI

俗话说说穷玩车 富玩表 只有XX玩电脑...
好吧, 这个确实是为了玩电脑弄的, 需求很简单: 一个安装了 Linux/Windows/macOS 多系统的电脑, 希望能远程重启切换
操作系统。所以这个玩意就诞生了

# grub-set-default-macos-windows

Read and change GRUB's saved default directly from Windows or macOS.

Native console applications built from the same C environment editor and
read-only libext2fs filesystem reader. No Python, WSL, Cygwin, Homebrew, ext4
mount driver, or separate debugfs executable is needed at runtime.

This version accepts an explicit menu entry rather than toggling fixed numbers:

```powershell
# Windows: Administrator PowerShell
.\grub-set-default.exe --dry-run 2
.\grub-set-default.exe 2
.\grub-set-default.exe 0
```

```sh
# macOS, after building below
sudo ./dist/grub-set-default --dry-run 4
sudo ./dist/grub-set-default 4
sudo ./dist/grub-set-default 0
```

Like GNU `grub-set-default`, it accepts a menu number, title, identifier, or
submenu path (quote values containing spaces or `>`), removes `next_entry` and
`prev_saved_entry`, and sets `saved_entry`. All other environment variables are
preserved. The existing Linux GRUB configuration must use `GRUB_DEFAULT=saved`.
Use `--list-entries` to inspect static menu definitions. Setting a default does
not validate that an entry exists; choose its number/title/ID from your GRUB configuration. There are no
hard-coded Linux, Windows or OpenCore entry numbers.

Progress messages are printed immediately to stderr. Discovery identifies each
partition being checked (including macOS `diskutil` queries), skipped partitions,
and the selected target. Reads report which GRUB file is being inspected; writes
report backup, recheck, write, cache-flush and verification stages. These are
console diagnostics; read-only operations still do not flush or write the disk.

Setting a default asks for confirmation, including for dry runs, unless `--yes`
is supplied. Read-only listing does not ask for write confirmation. The program
never reboots. Setting a default requires an existing, valid, traditional
1024-byte grubenv file. It does not create missing files or resize them.
By default, each eligible filesystem is probed for these layouts:

```text
/boot/grub/grubenv
/boot/grub2/grubenv
/grub/grubenv
/grub2/grubenv
```

This covers GRUB files on the root filesystem or a separate `/boot` partition.
A single valid match is selected automatically. Multiple matches, even on the
same partition, require selection of a partition/path pair; no path gets silent
preference. This also applies to images and explicitly selected partitions.

`--boot-directory DIR` (or `--boot-directory=DIR`) restricts probing to the
existing `DIR/grub/grubenv` convention. It means a path **inside the selected
ext4 filesystem**, rather than a mounted host directory. For example,
`--boot-directory /` restricts selection to `/grub/grubenv`; it does not select
`/grub2/grubenv`. Omit the option to discover `grub2` layouts automatically.
GNU's deprecated `--root-directory` option is not implemented.

## Choose the next boot without changing the permanent default

```sh
# macOS: use a menu number, title, ID, or submenu path from your configuration
sudo ./dist/grub-set-default --next-boot --dry-run opencore
sudo ./dist/grub-set-default --next-boot opencore
```

```powershell
# Windows: Administrator PowerShell
.\grub-set-default.exe --next-boot --dry-run 2
.\grub-set-default.exe --next-boot 2
```

`--next-boot` sets `next_entry` and preserves the existing `saved_entry`,
including an empty or absent value. It removes `prev_saved_entry` so this legacy
override cannot replace the permanent default at boot. Other variables and bytes
beyond grubenv are preserved. The same partition-selection options, confirmation,
backups, dry-run behavior, flush and read-back checks apply. It does not reboot.
Do not combine it with `--scan`, `--list-entries`, or `--show-default`.

The installed `grub.cfg` must honor `next_entry` and clear it using `save_env`,
and GRUB must be able to write the environment block at boot. Otherwise the
override may be ignored or persist across subsequent boots. The program cannot
verify these boot-time conditions. See the [GNU GRUB next_entry documentation](https://www.gnu.org/software/grub/manual/grub/html_node/next_005fentry.html).

For environments without a legacy `prev_saved_entry`, this performs the same
variable update as GNU `grub-reboot`. GNU's tool restores a nonempty legacy
`prev_saved_entry` into `saved_entry`; this option instead preserves the current
`saved_entry` and removes the legacy override. A later persistent-default command
(without `--next-boot`) clears `next_entry` as usual.

## Unattended use

`--yes` (or `-y`) skips confirmation for persistent-default and next-boot changes,
including dry runs. Backups, validation, locking, flush and verification still
apply. It never reboots. If multiple partition/path pairs match, it fails with a
nonzero exit status instead of choosing a target or prompting. An explicit
device or partition UUID restricts the partition, but multiple layouts on that
partition still count as ambiguous. `--boot-directory` can further restrict
selection to a specific traditional `grub` layout.

```sh
# macOS: substitute your partition UUID and menu entry
sudo ./dist/grub-set-default --partition-guid YOUR-PARTITION-UUID --yes --next-boot opencore
```

```powershell
# Windows: Administrator PowerShell
.\grub-set-default.exe --partition-guid YOUR-PARTITION-UUID --yes 2
```

`--yes --dry-run` validates and previews without waiting for input or writing to
the target. With read-only modes, `--yes` suppresses any interactive partition
selection by rejecting ambiguity; `--scan` continues to list all matches.

## Show the current stored default

```sh
# macOS
sudo ./dist/grub-set-default --show-default
sudo ./dist/grub-set-default --device /dev/rdisk0s3 --show-default
```

On Windows, use `grub-set-default.exe --show-default` in an Administrator
terminal. This mode prints `saved_entry`, `next_entry`, and `prev_saved_entry`
from grubenv. Missing variables are shown as `(unset)` and empty values as
`(empty)`. A nonempty `next_entry` is called out because it can override the
persistent default for the next boot. These are stored values; the actual GRUB
configuration determines how they affect boot selection.

The option is read-only, needs no menu entry argument, does not require
grub.cfg, and creates no backups or write confirmation. Existing partition
discovery, `--partition-guid`, `--boot-directory`, and `--image` options apply.
Do not combine it with `--list-entries`, `--scan`, or `--dry-run`.

## List menu entries without changing anything

On macOS, after building:

```sh
./build-macos.sh
sudo ./dist/grub-set-default --list-entries
# Restrict to a known partition if needed:
sudo ./dist/grub-set-default --device /dev/rdisk0s3 --list-entries
```

The Windows equivalent is `grub-set-default.exe --list-entries` in an
Administrator terminal. Both versions also accept `--image FILE` for testing
without administrative privileges. Do not provide a menu entry argument or
combine listing with `--scan` or `--dry-run`.

This mode probes for the sibling `grub.cfg` in each common layout directly on
ext4, independently of grubenv. It does not open the target for writing, create
backups, change the default, or reboot. If several layouts contain a config,
choose a partition/path pair from the discovery results. `--boot-directory DIR` also applies to listing;
use `--boot-directory /` to read `/grub/grub.cfg` on a separate /boot partition.

Example output:

```text
0  entry  title="Debian GNU/Linux"  id="debian"  line=10
1  submenu  title="Advanced options"  id="advanced"  line=20
1>0  entry  title="Recovery"  id="recovery"  line=21
2  entry  title="OpenCore"  id="opencore"  line=30
```

The parser handles quotes, escapes, comments, braces and nested submenus.
It extracts literal titles and IDs, including `--id=value`, `--id value`, and
the common `$menuentry_id_option value` syntax. Unexpanded variables are marked;
their values are not inferred. Indices reflect the definitions encountered and
are provisional, since conditions, functions and variables are not executed.
Included configs (`source`/`configfile`) and generated BLS entries are not
followed or expanded. This lists static definitions; it does not validate
whether an entry will appear or boot. Configuration files are limited to 4 MiB.
Malformed or unsupported syntax can return an error after a partial listing.

## Discovery and explicit targets

Both platforms offer `--scan` to list partition/path pairs without changing anything,
and `--partition-guid UUID` to restrict discovery. One candidate is selected
for inspection; multiple candidates require a choice before final confirmation,
or cause an error when `--yes` is used.

Windows discovers currently present disks through SetupAPI and reads GPT
partition tables. It checks common Linux filesystem/root/XBOOTLDR types and an
ext4 signature, then validates the filesystem and environment. An incomplete
disk scan stops automatic selection. You can restrict the disk with `--disk N`,
or explicitly select `--disk N --partition P --partition-guid UUID`.

macOS automatic discovery first runs `/usr/sbin/diskutil list -plist physical`.
It selects direct Linux partitions from physical GPT disks (both internal and
external), excluding synthesized APFS devices and disk images. Only those
Linux candidates receive individual `diskutil info -plist` calls to verify
their types, UUIDs and mount state. Progress shows the physical-disk count and
the Linux-partition count. A malformed list or failed enumeration stops the scan.
Mount-state checks require `diskutil` to report a boolean `Mounted` property
or a string `MountPoint`. Linux partitions may omit `Mounted` and report an
empty `MountPoint` instead, which is accepted as unmounted. If both properties
are absent, or either has the wrong type, access stops. A true `Mounted` or
nonempty `MountPoint` excludes the partition. These checks run again before writing.

`--device /dev/rdisk0s3` (or `/dev/disk0s3`) checks only that explicit partition,
without enumerating physical disks. This also permits explicitly selecting a
supported virtual Linux partition; all normal type, UUID, unmounted-state and
filesystem checks still apply. `--partition-guid UUID` can further restrict the
selection. Raw reads are aligned for the device sector size.

Encrypted containers, LVM, RAID and MBR disks are not automatically searched.
Only clean filesystems without pending journal recovery are supported.
Encrypted, inline-data, fs-verity, sparse and unwritten grubenv mappings are
refused. Metadata checksum verification remains enabled.

## Writes and backups

All filesystem traversal is read-only. The only partition write replaces the
existing allocated filesystem block containing grubenv. Bytes after the first
1024 bytes of that block are preserved. Reformatting the environment may move
variables within its first 1024 bytes, just as grub-editenv does.

Before confirmation, a private temporary directory holds the original block,
replacement block and location information. Successful writes, successful dry
runs and cancellations delete the backups; errors retain them for inspection.
Windows also saves a separate copy of the original 1024-byte environment.

Write access is requested only after confirmation. Windows requires a partition
lock; macOS checks that the partition UUID and unmounted state still match.
Both re-resolve the file mapping, compare the original block, flush the write,
and verify the complete block afterward. A changed target aborts the operation.
For an unchanged saved entry without override variables, no write is necessary.
On macOS raw devices, both `fsync` and the disk-specific
`ioctl(DKIOCSYNCHRONIZECACHE)` must succeed before read-back verification.
Interrupted calls are retried. Unsupported cache flushing is an error, as are
other flush failures; backups are retained. `F_FULLFSYNC` is no longer used on
raw disk handles. Image files use `fsync` without the disk ioctl.

## Build Windows on Debian

Install MinGW-w64 if necessary:

```sh
sudo apt install gcc-mingw-w64-x86-64
```

Run the build; it downloads and verifies e2fsprogs if needed:

```sh
./build-windows.sh
# An explicit archive path is also accepted:
./build-windows.sh /path/to/e2fsprogs-1.47.4.tar.gz
```

Output: `dist/grub-set-default.exe`, native x64 with GNU libraries linked in;
only Windows system DLLs are imported. No administrator rights are needed.
Network access and curl are needed only when the dependency is missing.
Both build scripts show each stage and stream configure/compiler/make output
to the terminal while saving logs. A failure identifies the stage and log path.

## Build natively on macOS

Clone or copy the source directory to macOS. The build downloads and verifies
the e2fsprogs archive if missing. For an offline build, copy the archive to the
parent directory or pass its path to the build script. Install Apple's
Command Line Tools if they are not already installed:

```sh
xcode-select --install
```

From the source directory:

```sh
./build-macos.sh
./dist/grub-set-default --help
sudo ./dist/grub-set-default --scan
sudo ./dist/grub-set-default --dry-run 4
```

Output: `dist/grub-set-default` for the Mac's native architecture. Dependencies
are built from the downloaded e2fsprogs sources; Homebrew is not used. Build logs
are in `build/macos/`. Build on the target Mac; the Debian Windows cross build
does not produce a macOS executable.

## Disposable image and tests

Both programs support `--image FILE`, a regular file containing a partition-only
ext4 filesystem. This mode needs no administrator/root privileges:

```sh
./dist/grub-set-default --image /path/to/ext4-partition.img --show-default
./dist/grub-set-default --image /path/to/ext4-partition.img --dry-run 0
```

Use a disposable image containing an existing grubenv in a supported layout.
On Windows, use `grub-set-default.exe` with the same options.
Windows image mode requires a normal local file path shorter than MAX_PATH.
Long/extended and UNC image paths are not supported.

On Debian, install the host test prerequisites and build the dependencies:

```sh
sudo apt install build-essential python3 e2fsprogs grub-common gcc-mingw-w64-x86-64
./build-windows.sh
./test.sh
```

Tests cover the editor under address/undefined behavior sanitizers, real ext4
images, Windows aligned reads and discovery through API mocks, and the POSIX
CLI's actual image-write path, confirmation, cleanup, arbitrary entries,
idempotence and GNU grub-editenv compatibility. No physical devices are opened.
The POSIX reader is used in those CLI tests, including its explicit empty
libext2fs options handling.

Native device operations require testing on the target operating system.
The Linux image tests exercise the shared core and POSIX front end; platform
mocks do not exercise real Windows device APIs or Apple's diskutil and
CoreFoundation APIs.

## CI builds and downloadable packages

The GitHub Actions workflow runs on branch pushes, pull requests, `v*` tag
pushes, and manual runs from the Actions tab. It performs:

* Linux host tests and cross-compilation of the Windows x64 executable.
* Native macOS builds and disposable-image tests on Intel and Apple Silicon.
* Native Windows tests of the packaged executable using a disposable image.

No physical disks are accessed by these tests. Windows x64 and Intel macOS builds
upload packages under the workflow run's **Artifacts** section. Apple Silicon
is build/test-only: CI does not package or publish an ARM64 macOS binary.
Packages are kept for 14 days;
build logs are kept for 7 days. Windows packages are ZIP files; Mac packages
are tar.gz files that preserve executable permissions. Each includes the binary,
README, licenses, and complete project and e2fsprogs source archives for rebuilding.
A separate SHA-256 checksum accompanies each package. A Windows preview can be
generated locally after building:

```sh
python3 ci/package.py --platform windows-x86_64 --version dev-preview
```

On a Mac, use `--platform macos-x86_64` or `--platform macos-arm64` to match
the built binary. Packages are written to `dist/packages/`.

After `./build-macos.sh`, run the native Mac tests with:

```sh
./test-macos.sh
```

To publish a version, commit and push the workflow and project first, then
create and push a version tag, for example:

```sh
git tag v2.0.0
git push origin v2.0.0
```

Only after all platform jobs succeed does the workflow create a GitHub Release
and attach the Windows x64 and Intel macOS packages and `SHA256SUMS`.
The release job explicitly downloads only those two packages. ARM64 macOS is
excluded from releases. Tags containing a hyphen
(such as `v2.0.0-rc1`) produce prereleases. An ordinary push or manual run does
not publish a release. No personal access token is needed: the release job uses
GitHub's built-in token with `contents: write`; build/test jobs have read-only
repository access. The binaries are not notarized or Authenticode-signed.

## Source and licensing

The source uses [e2fsprogs](https://git.kernel.org/pub/scm/fs/ext2/e2fsprogs.git/)
libraries; it is a purpose-built editor, not the full debugfs shell. The build
retains extracted upstream sources and license notices in `build/`. Preserve
the corresponding upstream sources and licenses when redistributing the
statically linked executable.
