"""Exercise a packaged executable with a disposable ext4 image, never devices.

Also accepts --program for checking the harness against the Linux image CLI.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

parser = argparse.ArgumentParser()
source = parser.add_mutually_exclusive_group(required=True)
source.add_argument("--packages", type=Path)
source.add_argument("--program", type=Path)
parser.add_argument("--fixture", type=Path, required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="grub-ci-") as temporary:
    base = Path(temporary)
    if args.packages:
        archives = list(args.packages.glob("*-windows-x86_64.zip"))
        assert len(archives) == 1, archives
        archive = archives[0]
        checksum = Path(str(archive) + ".sha256").read_text().split()[0]
        assert hashlib.sha256(archive.read_bytes()).hexdigest() == checksum
        with zipfile.ZipFile(archive) as stream:
            stream.extractall(base / "package")
        programs = list((base / "package").glob("*/grub-set-default.exe"))
        assert len(programs) == 1
        program = programs[0]
    else:
        program = args.program.resolve()
    image = base / "test.img"
    shutil.copyfile(args.fixture / "test.img", image)
    pristine = image.read_bytes()
    offset = json.loads((args.fixture / "fixture.json").read_text())["offset"]
    original = (args.fixture / "original-grubenv.bin").read_bytes()
    assert pristine[offset:offset + 1024] == original
    environment = dict(os.environ, TEMP=str(base), TMP=str(base), TMPDIR=str(base))

    def call(*arguments, answer=""):
        result = subprocess.run([str(program), "--image", str(image), *arguments],
                                input=answer, text=True, capture_output=True,
                                env=environment, timeout=60)
        print(result.stdout, end="")
        if result.returncode:
            print(result.stderr, end="")
        assert not list(base.glob("grubenv-backup-*")), "Backup cleanup failed"
        return result

    result = call("--show-default")
    assert result.returncode == 0 and "saved_entry=0" in result.stdout
    assert image.read_bytes() == pristine
    result = call("--list-entries")
    assert result.returncode == 0 and 'id="opencore"' in result.stdout
    assert image.read_bytes() == pristine
    result = call("--dry-run", "opencore", answer="y\n")
    assert result.returncode == 0 and "[y/N]" in result.stdout
    assert image.read_bytes() == pristine
    result = call("opencore", answer="n\n")
    assert result.returncode == 0 and image.read_bytes() == pristine
    for flags, expected in (
            (("--yes", "--next-boot", "opencore"), b"# GRUB Environment Block\nsaved_entry=0\nnext_entry=opencore\ncustom=keep\n"),
            (("--yes", "linux"), b"# GRUB Environment Block\nsaved_entry=linux\ncustom=keep\n")):
        result = call(*flags)
        assert result.returncode == 0 and "Verified" in result.stdout
        assert "[y/N]" not in result.stdout
        actual = image.read_bytes()
        assert actual[:offset] == pristine[:offset]
        assert actual[offset + 1024:] == pristine[offset + 1024:]
        assert actual[offset:offset + 1024] == expected + b"#" * (1024 - len(expected))
        result = call(*flags)
        assert result.returncode == 0 and "Already set" in result.stdout
        assert image.read_bytes() == actual
    result = call("--yes", "bad\nentry")
    assert result.returncode != 0 and image.read_bytes() == actual
print("Packaged executable passed image tests: read-only modes, confirmation, cancellation, next-boot/persistent writes, no-op, validation, cleanup and byte preservation.")
