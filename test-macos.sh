#!/bin/bash
# Native tests use temporary regular files only; never sudo or physical disks.
set -euo pipefail
project=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
[ "$(uname -s)" = Darwin ] || { echo 'Run this test script on macOS.' >&2; exit 1; }
[ -x "$project/dist/grub-set-default" ] || { echo 'Run ./build-macos.sh first.' >&2; exit 1; }
. "$project/build-dependencies.sh"
build="$project/build/macos"
run_build_step 'Building debugfs for test fixtures' "$build/build.log" make -j4 -C "$build/debugfs" debugfs
run_build_step 'Building mke2fs for test fixtures' "$build/build.log" make -j4 -C "$build/misc" mke2fs
for unit in grubenv menu_entries; do
  run_build_step "Building $unit regression tests" "$build/build.log" \
    cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I"$project" \
    "$project/$unit.c" "$project/tests/test_$unit.c" -o "$build/test-$unit"
  ASAN_OPTIONS=detect_leaks=0 "$build/test-$unit"
done
MKE2FS_CONFIG="$build/misc/mke2fs.conf" python3 "$project/tests/cli_integration.py" \
  "$build/misc/mke2fs" "$build/debugfs/debugfs" "$project/dist/grub-set-default"
