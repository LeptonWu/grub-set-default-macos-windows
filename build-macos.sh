#!/bin/bash
# Build natively on macOS with Apple Command Line Tools, no Homebrew required.
set -euo pipefail
project=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
archive=${1:-"$project/../e2fsprogs-1.47.4.tar.gz"}
source_dir="$project/build/e2fsprogs-1.47.4"
build="$project/build/macos"
[ "$(uname -s)" = Darwin ] || { echo 'Run this build script on macOS.' >&2; exit 1; }
command -v cc >/dev/null || { echo 'Install Apple Command Line Tools: xcode-select --install' >&2; exit 1; }
. "$project/build-dependencies.sh"
printf 'Building grub-set-default for macOS (%s)...\n' "$(uname -m)"
ensure_e2fsprogs_source "$archive" "$project/build"
mkdir -p "$build" "$project/dist"
cd "$build"
if [ ! -f Makefile ]; then
  : > configure.log
  run_build_step 'Configuring e2fsprogs for macOS' "$build/configure.log" \
    "$source_dir/configure" --disable-nls --disable-fuse2fs --without-libarchive \
    --enable-libuuid --enable-libblkid \
    --disable-bsd-shlibs --disable-elf-shlibs --disable-e2initrd-helper \
    --disable-uuidd --without-systemd-unit-dir --without-crond-dir \
    'MKDIR_P=mkdir -p'
else
  printf 'Reusing macOS dependency configuration.\n'
fi
: > build.log
run_build_step 'Building e2fsprogs libraries (4 parallel jobs)' "$build/build.log" make -j4 libs
for unit in macos_disks menu_entries config_reader posix_main posix_reader posix_flush grubenv ext4_reader; do
  run_build_step "Compiling $unit.c" "$build/build.log" \
    cc -std=c11 -Wall -Wextra -Werror -O2 -I"$build/lib" -I"$source_dir/lib" \
    -c "$project/$unit.c" -o "$unit.o"
done
run_build_step 'Linking grub-set-default' "$build/build.log" \
  cc -o "$project/dist/grub-set-default" macos_disks.o menu_entries.o config_reader.o posix_main.o posix_reader.o posix_flush.o grubenv.o ext4_reader.o \
  lib/libext2fs.a lib/libcom_err.a lib/libuuid.a -framework CoreFoundation
printf 'Built: %s\n' "$project/dist/grub-set-default"
printf 'Build logs: %s\n' "$build"
