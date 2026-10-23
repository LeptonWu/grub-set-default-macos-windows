"""Exercise the real libext2fs reader on disposable ext4 filesystems."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile


def run(*command):
    return subprocess.run(command, capture_output=True, text=True, env=os.environ)


mk, debug, inspect = sys.argv[1:]
count = 0
with tempfile.TemporaryDirectory() as temporary:
    base = Path(temporary)
    for block_size in (1024, 4096):
        for current in ("0", "2"):
            root = base / f"root-{block_size}-{current}"
            directory = root / "boot/grub"
            directory.mkdir(parents=True)
            content = (
                f"# GRUB Environment Block\nsaved_entry={current}\n"
                "next_entry=preserve-me\n"
            ).encode()
            original = content + b"#" * (1024 - len(content))
            (directory / "grubenv").write_bytes(original)
            image = base / f"{block_size}-{current}.img"
            with image.open("wb") as stream:
                stream.truncate(64 * 1024 * 1024)
            result = run(mk, "-q", "-t", "ext4", "-F", "-b", str(block_size),
                         "-O", "metadata_csum,64bit", "-d", str(root), str(image))
            assert result.returncode == 0, result.stderr
            result = run(inspect, str(image))
            assert result.returncode == 0, result.stdout + result.stderr
            size, physical, offset, before, after = result.stdout.split()
            assert int(size) == block_size and before == current
            assert after == "test-entry"
            assert int(offset) == int(physical) * block_size
            result = run(debug, "-R", "bmap /boot/grub/grubenv 0", str(image))
            assert int(result.stdout.strip()) == int(physical)
            with image.open("rb") as stream:
                stream.seek(int(offset))
                assert stream.read(1024) == original
            count += 1

    # Windows raw device names contain '?'. Use a literal '?' in a host file
    # name to exercise libext2fs option parsing with the real library too.
    literal_question = base / "device?literal.img"
    literal_question.write_bytes(image.read_bytes())
    result = run(inspect, str(literal_question))
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.split()[-2:] == ["2", "test-entry"]
    count += 1

    pristine = image.read_bytes()
    failures = {
        "dirty": "ssv state 0",
        "missing": "rm /boot/grub/grubenv",
        "wrong-size": "sif /boot/grub/grubenv size 100",
        "encrypted": "sif /boot/grub/grubenv flags 0x80800",
    }
    for name, command in failures.items():
        altered = base / f"{name}.img"
        altered.write_bytes(pristine)
        result = run(debug, "-w", "-R", command, str(altered))
        assert result.returncode == 0, result.stderr
        result = run(inspect, str(altered))
        assert result.returncode != 0, (name, result.stdout, result.stderr)
        count += 1
    altered = base / "bad-checksum.img"
    damaged = bytearray(pristine)
    damaged[1024 + 120] ^= 1  # Change superblock content without updating its checksum.
    altered.write_bytes(damaged)
    result = run(inspect, str(altered))
    assert result.returncode != 0, result.stdout + result.stderr
    count += 1

print(f"Passed {count} real ext4 image cases: dynamic lookup, defaults, 1K/4K blocks, "
      "dirty/missing/wrong-size/encrypted files and checksum rejection.")
