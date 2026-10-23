#!/usr/bin/env bash
# Cross-compile on Debian; download the dependency only if it is missing.
set -euo pipefail
project=$(cd -- "$(dirname -- "$0")" && pwd)
archive=${1:-"$project/../e2fsprogs-1.47.4.tar.gz"}
build="$project/build/windows"
source_dir="$project/build/e2fsprogs-1.47.4"
compiler=x86_64-w64-mingw32-gcc

command -v "$compiler" >/dev/null || {
  printf 'Install MinGW first: sudo apt install gcc-mingw-w64-x86-64\n' >&2; exit 1;
}
. "$project/build-dependencies.sh"
ensure_e2fsprogs_source "$archive" "$project/build"
mkdir -p "$build" "$project/dist"
cd "$build"
if [[ ! -f Makefile ]]; then
  "$source_dir/configure" --host=x86_64-w64-mingw32 \
    --disable-nls --disable-fuse2fs --without-libarchive \
    --disable-bsd-shlibs --disable-elf-shlibs --disable-e2initrd-helper \
    --disable-uuidd --without-systemd-unit-dir --without-crond-dir \
    'MKDIR_P=mkdir -p' > configure.log 2>&1
fi
# Follow upstream Windows CI: libss and the full debugfs shell are not needed.
make -j4 top-deps > build.log 2>&1
for library in et uuid blkid ext2fs; do
  make -j4 -C "lib/$library" all >> build.log 2>&1
done
for unit in menu_entries config_reader main grubenv ext4_reader windows_reader discovery; do
  "$compiler" -std=c11 -Wall -Wextra -Werror -O2 \
    -I"$build/lib" -I"$source_dir/lib" \
    -c "$project/$unit.c" -o "$unit.o"
done
"$compiler" -static -o "$project/dist/grub-set-default.exe" \
  menu_entries.o config_reader.o main.o grubenv.o ext4_reader.o windows_reader.o discovery.o \
  lib/libext2fs.a lib/libcom_err.a lib/libuuid.a -lrpcrt4 -lsetupapi -lwinpthread
x86_64-w64-mingw32-objdump -p "$project/dist/grub-set-default.exe" \
  > "$project/dist/pe-imports.txt"
if grep -iE 'DLL Name:.*(cygwin|msys|libgcc|libwinpthread)' "$project/dist/pe-imports.txt"; then
  printf 'Unexpected external runtime dependency.\n' >&2; exit 1
fi
printf 'Built: %s\n' "$project/dist/grub-set-default.exe"
printf 'Build logs: %s\n' "$build"
