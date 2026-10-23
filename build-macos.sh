#!/bin/sh
# Build natively on macOS with Apple Command Line Tools, no Homebrew required.
set -eu
project=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
archive=${1:-"$project/../e2fsprogs-1.47.4.tar.gz"}
source_dir="$project/build/e2fsprogs-1.47.4"
build="$project/build/macos"
[ "$(uname -s)" = Darwin ] || { echo 'Run this build script on macOS.' >&2; exit 1; }
command -v cc >/dev/null || { echo 'Install Apple Command Line Tools: xcode-select --install' >&2; exit 1; }
. "$project/build-dependencies.sh"
ensure_e2fsprogs_source "$archive" "$project/build"
mkdir -p "$build" "$project/dist"
cd "$build"
if [ ! -f Makefile ]; then
  "$source_dir/configure" --disable-nls --disable-fuse2fs --without-libarchive \
    --enable-libuuid --enable-libblkid \
    --disable-bsd-shlibs --disable-elf-shlibs --disable-e2initrd-helper \
    --disable-uuidd --without-systemd-unit-dir --without-crond-dir \
    'MKDIR_P=mkdir -p' > configure.log 2>&1
fi
make -j4 libs > build.log 2>&1
for unit in macos_disks menu_entries config_reader posix_main posix_reader posix_flush grubenv ext4_reader; do
  cc -std=c11 -Wall -Wextra -Werror -O2 -I"$build/lib" -I"$source_dir/lib" \
    -c "$project/$unit.c" -o "$unit.o"
done
cc -o "$project/dist/grub-set-default" macos_disks.o menu_entries.o config_reader.o posix_main.o posix_reader.o posix_flush.o grubenv.o ext4_reader.o \
  lib/libext2fs.a lib/libcom_err.a lib/libuuid.a -framework CoreFoundation
printf 'Built: %s\n' "$project/dist/grub-set-default"
