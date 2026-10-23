#!/usr/bin/env bash
# Run host-side tests using disposable files only; no sudo or physical devices.
set -euo pipefail
project=$(cd -- "$(dirname -- "$0")" && pwd)
source_dir="$project/build/e2fsprogs-1.47.4"
host="$project/build/host-tests"
if [[ ! -f "$source_dir/configure" ]]; then
  printf 'Run ./build-windows.sh first to extract the e2fsprogs sources.\n' >&2
  exit 1
fi
mkdir -p "$host"
cd "$host"
if [[ ! -f Makefile ]]; then
  "$source_dir/configure" --disable-nls --disable-fuse2fs --without-libarchive \
    --disable-bsd-shlibs --disable-elf-shlibs --disable-e2initrd-helper \
    --disable-uuidd --without-systemd-unit-dir --without-crond-dir \
    'MKDIR_P=mkdir -p' > configure.log 2>&1
fi
make -j4 libs > build.log 2>&1
make -j4 -C debugfs debugfs >> build.log 2>&1
make -j4 -C misc mke2fs >> build.log 2>&1
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I"$project" "$project/grubenv.c" "$project/tests/test_grubenv.c" -o test-grubenv
ASAN_OPTIONS=detect_leaks=0 ./test-grubenv
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I"$project" "$project/menu_entries.c" "$project/tests/test_menu_entries.c" -o test-menu-entries
ASAN_OPTIONS=detect_leaks=0 ./test-menu-entries
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -DDKIOCSYNCHRONIZECACHE=1234 -Dfsync=mock_fsync -Dioctl=mock_ioctl -I"$project" \
  "$project/posix_flush.c" "$project/tests/test_posix_flush.c" -o test-posix-flush
ASAN_OPTIONS=detect_leaks=0 ./test-posix-flush
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I"$project/tests/fake_macos" -I"$project" \
  "$project/macos_disks.c" "$project/tests/test_macos_disks.c" -o test-macos-disks
ASAN_OPTIONS=detect_leaks=0 ./test-macos-disks
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I"$project/tests/fake_win32" -I"$project" -I"$host/lib" -I"$source_dir/lib" \
  "$project/windows_reader.c" "$project/tests/test_windows_reader.c" \
  lib/libext2fs.a lib/libcom_err.a lib/libuuid.a -lpthread -o test-windows-reader
ASAN_OPTIONS=detect_leaks=0 ./test-windows-reader
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I"$project/tests/fake_win32" -I"$project" -I"$host/lib" -I"$source_dir/lib" \
  "$project/discovery.c" "$project/grubenv.c" "$project/tests/test_discovery.c" -o test-discovery
ASAN_OPTIONS=detect_leaks=0 ./test-discovery
cc -std=c11 -Wall -Wextra -Werror -O2 \
  -I"$project" -I"$host/lib" -I"$source_dir/lib" \
  "$project/ext4_reader.c" "$project/grubenv.c" "$project/tests/inspect_image.c" \
  lib/libext2fs.a lib/libcom_err.a lib/libuuid.a -lpthread -o inspect-image
MKE2FS_CONFIG="$host/misc/mke2fs.conf" python3 "$project/tests/integration.py" \
  "$host/misc/mke2fs" "$host/debugfs/debugfs" "$host/inspect-image"
# Build the same macOS front end on Linux in image-only mode, using the actual
# POSIX read adapter. This exercises the write path without physical devices.
cc -std=c11 -Wall -Wextra -Werror -O2 -DGRUB_USE_POSIX_READER \
  -I"$project" -I"$host/lib" -I"$source_dir/lib" \
  "$project/config_reader.c" "$project/menu_entries.c" "$project/posix_main.c" "$project/posix_reader.c" "$project/posix_flush.c" "$project/ext4_reader.c" "$project/grubenv.c" \
  lib/libext2fs.a lib/libcom_err.a lib/libuuid.a -lpthread -o grub-set-default
MKE2FS_CONFIG="$host/misc/mke2fs.conf" python3 "$project/tests/cli_integration.py" \
  "$host/misc/mke2fs" "$host/debugfs/debugfs" "$host/grub-set-default"
