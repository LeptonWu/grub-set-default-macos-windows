# Shared dependency setup and logging; build scripts run with Bash pipefail.
run_build_step() (
  build_step=$1
  build_log=$2
  shift 2
  printf '\n==> %s\nLog: %s\n' "$build_step" "$build_log"
  # Keep the command's failure visible even when tee succeeds.
  set -o pipefail
  if "$@" 2>&1 | tee -a "$build_log"; then
    printf '==> Completed: %s\n' "$build_step"
  else
    build_status=$?
    printf '\nBuild failed: %s (exit %s). See %s\n' "$build_step" "$build_status" "$build_log" >&2
    exit "$build_status"
  fi
)

verify_e2fsprogs_archive() {
  printf 'Verifying e2fsprogs archive: %s\n' "$1"
  if command -v sha256sum >/dev/null 2>&1; then
    dependency_digest=$(sha256sum "$1") || return 1
  elif command -v shasum >/dev/null 2>&1; then
    dependency_digest=$(shasum -a 256 "$1") || return 1
  else
    printf 'Need sha256sum or shasum to verify the e2fsprogs source.\n' >&2
    return 1
  fi
  dependency_digest=${dependency_digest%% *}
  if [ "$dependency_digest" != da274408bebbfd13a5a2fc3cfc66e3ffff17c48534673aa67f88d49b99123b96 ]; then
    printf 'SHA-256 mismatch for %s; remove it and retry, or supply the correct archive.\n' "$1" >&2
    return 1
  fi
}

ensure_e2fsprogs_source() (
  dependency_archive=$1
  dependency_build=$2
  dependency_temp=
  # A subshell keeps traps and temporary variables out of the calling build.
  trap '[ -z "$dependency_temp" ] || rm -f -- "$dependency_temp"' 0
  trap 'exit 1' HUP INT TERM
  if [ -f "$dependency_build/e2fsprogs-1.47.4/configure" ]; then
    printf 'Reusing e2fsprogs sources: %s/e2fsprogs-1.47.4\n' "$dependency_build"
    exit 0
  fi
  if [ ! -f "$dependency_archive" ]; then
    command -v curl >/dev/null 2>&1 || {
      printf 'Install curl, or supply the e2fsprogs archive as the first argument.\n' >&2
      exit 1
    }
    mkdir -p -- "$(dirname -- "$dependency_archive")" || exit 1
    dependency_temp=$(mktemp "${dependency_archive}.download.XXXXXXXX") || exit 1
    printf 'Downloading e2fsprogs 1.47.4 to %s\n' "$dependency_archive"
    curl --fail --location --retry 2 --connect-timeout 30 \
      https://www.kernel.org/pub/linux/kernel/people/tytso/e2fsprogs/v1.47.4/e2fsprogs-1.47.4.tar.gz \
      --output "$dependency_temp" || exit 1
    verify_e2fsprogs_archive "$dependency_temp" || exit 1
    mv -f -- "$dependency_temp" "$dependency_archive" || exit 1
    dependency_temp=
  else
    verify_e2fsprogs_archive "$dependency_archive" || exit 1
  fi
  mkdir -p -- "$dependency_build" || exit 1
  printf 'Extracting e2fsprogs sources into %s\n' "$dependency_build"
  tar -xzf "$dependency_archive" -C "$dependency_build" || exit 1
  [ -f "$dependency_build/e2fsprogs-1.47.4/configure" ] || {
    printf 'The extracted e2fsprogs source is incomplete.\n' >&2
    exit 1
  }
  printf 'Dependency sources ready.\n'
)
