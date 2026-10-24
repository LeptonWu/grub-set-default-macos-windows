"""Package a built binary, notices and complete rebuild sources (stdlib only)."""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tarfile
import tempfile
import zipfile

PROJECT = Path(__file__).resolve().parent.parent
VERSION = "1.47.4"
DIGEST = "da274408bebbfd13a5a2fc3cfc66e3ffff17c48534673aa67f88d49b99123b96"
parser = argparse.ArgumentParser()
parser.add_argument("--platform", choices=("windows-x86_64", "macos-x86_64", "macos-arm64"), required=True)
parser.add_argument("--version", required=True)
parser.add_argument("--archive", type=Path, default=PROJECT.parent / f"e2fsprogs-{VERSION}.tar.gz")
args = parser.parse_args()
if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", args.version):
    parser.error("Version must contain only letters, digits, dots, underscores and hyphens.")
if hashlib.sha256(args.archive.read_bytes()).hexdigest() != DIGEST:
    raise SystemExit("Dependency archive checksum mismatch; refusing to package.")
windows = args.platform == "windows-x86_64"
binary = PROJECT / "dist" / ("grub-set-default.exe" if windows else "grub-set-default")
if not binary.is_file():
    raise SystemExit(f"Build the binary first: {binary}")
data = binary.read_bytes()
if windows:
    pe_offset = struct.unpack_from("<I", data, 0x3c)[0] if len(data) >= 64 else len(data)
    valid_binary = (data[:2] == b"MZ" and pe_offset + 6 <= len(data)
                    and data[pe_offset:pe_offset + 4] == b"PE\0\0"
                    and struct.unpack_from("<H", data, pe_offset + 4)[0] == 0x8664)
else:
    expected_cpu = 0x01000007 if args.platform == "macos-x86_64" else 0x0100000c
    valid_binary = (len(data) >= 8 and data[:4] == b"\xcf\xfa\xed\xfe"
                    and struct.unpack_from("<I", data, 4)[0] == expected_cpu)
if not valid_binary:
    raise SystemExit(f"Binary format/architecture does not match {args.platform}.")
upstream = PROJECT / "build" / f"e2fsprogs-{VERSION}"
destination = PROJECT / "dist/packages"
destination.mkdir(parents=True, exist_ok=True)
name = f"grub-set-default-{args.version}-{args.platform}"
with tempfile.TemporaryDirectory(prefix="grub-package-") as temporary:
    stage = Path(temporary) / name
    stage.mkdir()
    shutil.copy2(binary, stage / binary.name)
    for document in ("README.md", "DEPENDENCIES.md", "LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(PROJECT / document, stage / document)
    notices = stage / "licenses"
    notices.mkdir()
    for relative, target in (("NOTICE", "e2fsprogs-NOTICE.txt"),
                             ("lib/uuid/COPYING", "libuuid-COPYING.txt"),
                             ("lib/uuid/gen_uuid.c", "libuuid-gen_uuid.c"),
                             ("lib/et/error_message.c", "libcom_err-error_message.c")):
        shutil.copy2(upstream / relative, notices / target)
    if windows:
        for source, target in (
                ("/usr/share/doc/mingw-w64-common/copyright", "MinGW-w64-copyright.txt"),
                ("/usr/share/doc/gcc-mingw-w64-base/copyright", "GCC-MinGW-copyright.txt"),
                ("/usr/share/common-licenses/GPL-3", "GPL-3.txt")):
            shutil.copy2(source, notices / target)
    sources = stage / "source"
    sources.mkdir()
    shutil.copy2(args.archive, sources / f"e2fsprogs-{VERSION}.tar.gz")
    # Explicit source roots avoid including generated builds or local credentials.
    files = set()
    for pattern in ("*.c", "*.h", "*.sh", "*.md", "LICENSE", ".gitignore"):
        files.update(PROJECT.glob(pattern))
    for directory in ("tests", "ci", ".github"):
        files.update((PROJECT / directory).rglob("*"))
    with tarfile.open(sources / "grub-set-default-source.tar.gz", "w:gz") as archive:
        for source in sorted(files):
            if source.is_file() and "__pycache__" not in source.parts and source.suffix != ".pyc":
                archive.add(source, arcname=str(Path("grub-set-default-macos-windows") / source.relative_to(PROJECT)))
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=PROJECT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=PROJECT))
    (stage / "BUILD_INFO.txt").write_text(f"Version: {args.version}\nPlatform: {args.platform}\nCommit: {commit}\nWorking tree: {'modified' if dirty else 'clean'}\ne2fsprogs: {VERSION}\n")
    if windows:
        output = destination / f"{name}.zip"
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for source in sorted(stage.rglob("*")):
                if source.is_file():
                    archive.write(source, source.relative_to(stage.parent))
    else:
        output = destination / f"{name}.tar.gz"
        with tarfile.open(output, "w:gz") as archive:
            archive.add(stage, arcname=name)
checksum = hashlib.sha256(output.read_bytes()).hexdigest()
Path(str(output) + ".sha256").write_text(f"{checksum}  {output.name}\n")
print(f"Created {output}\nSHA-256: {checksum}")
