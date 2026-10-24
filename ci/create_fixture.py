"""Create a portable Windows test fixture on Linux, using regular files only."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

mk, debug, destination = map(Path, sys.argv[1:])
destination.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    grub = root / "boot/grub"
    grub.mkdir(parents=True)
    contents = b"# GRUB Environment Block\nsaved_entry=0\nnext_entry=\nprev_saved_entry=4\ncustom=keep\n"
    original = contents + b"#" * (1024 - len(contents))
    (grub / "grubenv").write_bytes(original)
    (grub / "grub.cfg").write_text("menuentry 'Debian' --id linux { true; }\nmenuentry 'OpenCore' --id opencore { true; }\n")
    image = destination / "test.img"
    with image.open("wb") as stream:
        stream.truncate(64 * 1024 * 1024)
    subprocess.run([str(mk), "-q", "-t", "ext4", "-F", "-b", "4096", "-d", str(root), str(image)], check=True)
    result = subprocess.run([str(debug), "-R", "bmap /boot/grub/grubenv 0", str(image)],
                            text=True, capture_output=True, check=True)
    (destination / "fixture.json").write_text(json.dumps({"offset": int(result.stdout.strip()) * 4096}))
    (destination / "original-grubenv.bin").write_bytes(original)
print(f"Created Windows image fixture in {destination}")
