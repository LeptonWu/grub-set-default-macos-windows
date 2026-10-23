"""Run the native POSIX CLI on disposable ext4 images, never physical disks."""
from pathlib import Path
import hashlib
import os
import subprocess
import sys
import tempfile

mk, debug, program = map(str, map(Path, sys.argv[1:]))


def call(*args, answer="y\n", env=None):
    return subprocess.run([program, *args], input=answer, text=True,
                          capture_output=True, env=env)


with tempfile.TemporaryDirectory() as temporary:
    base = Path(temporary)
    root = base / "root"
    directory = root / "boot/grub"
    directory.mkdir(parents=True)
    text = (b"# GRUB Environment Block\nsaved_entry=0\nnext_entry=4\n"
            b"prev_saved_entry=2\nrecordfail=1\ncustom=a=b#c\n")
    original_env = text + b"#" * (1024 - len(text))
    (directory / "grubenv").write_bytes(original_env)
    config = ("# menuentry 'Commented' { }\n"
              "menuentry 'Debian GNU/Linux' --id debian { echo '${root}'; }\n"
              "submenu 'Advanced' --id advanced {\n"
              " menuentry 'Recovery' --id recovery { true; }\n"
              "}\n"
              "menuentry 'OpenCore' $menuentry_id_option 'opencore' { chainloader /EFI/OC/OpenCore.efi; }\n")
    # Exercise a configuration spanning many filesystem blocks.
    (directory / "grub.cfg").write_text(config + "# padding\n" * 8192)
    separate_boot = root / "grub"
    separate_boot.mkdir()
    (separate_boot / "grub.cfg").write_text(config)
    (separate_boot / "grubenv").write_bytes(original_env)
    image = base / "test.img"
    with image.open("wb") as f:
        f.truncate(64 * 1024 * 1024)
    subprocess.run([mk, "-q", "-t", "ext4", "-F", "-b", "4096", "-d", str(root), str(image)], check=True)
    result = subprocess.run([debug, "-R", "bmap /boot/grub/grubenv 0", str(image)], capture_output=True, text=True, check=True)
    offset = int(result.stdout.strip()) * 4096
    pristine = image.read_bytes()
    env = dict(os.environ, TMPDIR=str(base))

    def clean():
        assert not list(base.glob("grubenv-backup-*"))

    result = call("--image", str(image), "--show-default", answer="", env=env)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "saved_entry=0\nnext_entry=4\nprev_saved_entry=2\n" in result.stdout
    assert "override" in result.stdout and "[y/N]" not in result.stdout
    assert image.read_bytes() == pristine
    clean()
    # Valid full environments must remain readable even if no saved_entry edit fits.
    for prefix, padding, expected in (
            (b"# GRUB Environment Block\ncustom=", 3, "(unset)"),
            (b"# GRUB Environment Block\nsaved_entry=\ncustom=", 0, "(empty)")):
        crowded = prefix + b"x" * (1024 - padding - len(prefix) - 1) + b"\n" + b"#" * padding
        with image.open("r+b") as f:
            f.seek(offset)
            f.write(crowded)
        before = image.read_bytes()
        result = call("--image", str(image), "--show-default", answer="", env=env)
        assert result.returncode == 0, result.stdout + result.stderr
        assert f"saved_entry={expected}\n" in result.stdout
        assert "[y/N]" not in result.stdout and image.read_bytes() == before
        clean()
    image.write_bytes(pristine)
    result = call("--image", str(image), "--list-entries", answer="", env=env)
    assert result.returncode == 0, result.stdout + result.stderr
    assert '0  entry  title="Debian GNU/Linux"  id="debian"' in result.stdout
    assert '1>0  entry  title="Recovery"  id="recovery"' in result.stdout
    assert '2  entry  title="OpenCore"  id="opencore"' in result.stdout
    assert "Commented" not in result.stdout and "[y/N]" not in result.stdout
    assert image.read_bytes() == pristine
    clean()
    result = call("--image", str(image), "--list-entries", "--boot-directory", "/", answer="", env=env)
    assert result.returncode == 0 and "Configuration: /grub/grub.cfg" in result.stdout, result
    clean()
    result = call("--image", str(image), "--dry-run", "2", env=env)
    assert result.returncode == 0 and "[y/N]" in result.stdout, result
    assert image.read_bytes() == pristine
    clean()
    result = call("--image", str(image), "2", answer="n\n", env=env)
    assert result.returncode == 0 and "Cancelled" in result.stdout, result
    assert image.read_bytes() == pristine
    clean()
    result = call("--image", str(image), "2", answer="", env=env)
    assert result.returncode == 0 and image.read_bytes() == pristine
    clean()
    # Next-boot selection uses the same confirmation and cleanup rules.
    for flags, answer in ((("--dry-run",), "y\n"), ((), "n\n"), ((), "")):
        result = call("--image", str(image), "--next-boot", *flags, "opencore", answer=answer, env=env)
        assert result.returncode == 0 and "[y/N]" in result.stdout, result
        assert image.read_bytes() == pristine
        clean()
    for flags in (("--scan",), ("--list-entries",), ("--show-default",),
                  ("--next-boot",), ("2",)):
        result = call("--image", str(image), "--next-boot", *flags, "opencore", env=env)
        assert result.returncode != 0 and image.read_bytes() == pristine, result
        clean()
    for args in (("--next-boot",), ("--next-boot", ""), ("--next-boot", "bad\nentry")):
        result = call("--image", str(image), *args, env=env)
        assert result.returncode != 0 and image.read_bytes() == pristine, result
        clean()
    for entry in ("2", "Advanced options>Recovery", "opencore"):
        result = call("--image", str(image), "--next-boot", entry, env=env)
        assert result.returncode == 0 and "next_entry" in result.stdout, result
        actual = image.read_bytes()
        expected = ("# GRUB Environment Block\nsaved_entry=0\nnext_entry=" + entry +
                    "\nrecordfail=1\ncustom=a=b#c\n").encode()
        assert actual[offset:offset + 1024] == expected + b"#" * (1024 - len(expected))
        assert actual[:offset] == pristine[:offset] and actual[offset + 1024:] == pristine[offset + 1024:]
        clean()
        # Compare actual GNU grub-reboot on a disposable boot directory. Remove
        # legacy prev_saved_entry first: GNU restores it, whereas our mode
        # intentionally preserves the existing saved_entry and drops the override.
        reference_editor = Path("/usr/bin/grub-editenv")
        reference_reboot = Path("/usr/sbin/grub-reboot")
        if reference_editor.exists() and reference_reboot.exists():
            reference_boot = base / "reference-boot"
            reference_env = reference_boot / "grub/grubenv"
            reference_env.parent.mkdir(parents=True, exist_ok=True)
            reference_env.write_bytes(original_env)
            subprocess.run([str(reference_editor), str(reference_env), "unset", "prev_saved_entry"], check=True)
            subprocess.run([str(reference_reboot), "--boot-directory", str(reference_boot), entry],
                           check=True, capture_output=True, text=True)
            assert reference_env.read_bytes() == actual[offset:offset + 1024]
    result = call("--image", str(image), "--next-boot", "opencore", env=env)
    assert result.returncode == 0 and "Already set" in result.stdout, result
    assert image.read_bytes() == actual
    clean()
    result = call("--image", str(image), "--show-default", answer="", env=env)
    assert result.returncode == 0 and "saved_entry=0\nnext_entry=opencore\nprev_saved_entry=(unset)" in result.stdout
    assert image.read_bytes() == actual
    clean()
    image.write_bytes(pristine)
    for entry in ("2", "0", "4", "12", "Advanced options>GNU/Linux", "gnulinux-id", "gnulinux-id"):
        result = call("--image", str(image), entry, env=env)
        assert result.returncode == 0, result.stdout + result.stderr
        actual = image.read_bytes()
        assert actual[:offset] == pristine[:offset]
        assert actual[offset + 1024:] == pristine[offset + 1024:]
        expected = ("# GRUB Environment Block\nsaved_entry=" + entry + "\nrecordfail=1\ncustom=a=b#c\n").encode()
        assert actual[offset:offset + 1024] == expected + b"#" * (1024 - len(expected))
        clean()
    # Compare the result with the same three grub-editenv operations used by
    # Debian's grub-set-default, if the reference tool is installed.
    reference = Path("/usr/bin/grub-editenv")
    if reference.exists():
        ref = base / "reference"
        ref.write_bytes(original_env)
        for arguments in (("unset", "prev_saved_entry"), ("unset", "next_entry"), ("set", "saved_entry=gnulinux-id")):
            subprocess.run([str(reference), str(ref), *arguments], check=True)
        assert ref.read_bytes() == actual[offset:offset + 1024]
    result = call("--image", str(image), "--show-default", answer="", env=env)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "saved_entry=gnulinux-id\nnext_entry=(unset)\nprev_saved_entry=(unset)" in result.stdout
    assert image.read_bytes() == actual
    clean()
    # Reading defaults neither requires nor parses a configuration file.
    subprocess.run([debug, "-w", "-R", "rm /grub/grub.cfg", str(image)], check=True, capture_output=True)
    before = image.read_bytes()
    result = call("--image", str(image), "--show-default", "--boot-directory", "/", answer="", env=env)
    assert result.returncode == 0 and "Environment: /grub/grubenv" in result.stdout, result
    assert "saved_entry=0\nnext_entry=4\n" in result.stdout
    assert image.read_bytes() == before
    clean()
    for args in (("--show-default", "2"), ("--show-default", "--dry-run"),
                 ("--show-default", "--scan"), ("--show-default", "--list-entries")):
        result = call("--image", str(image), *args, answer="", env=env)
        assert result.returncode != 0 and image.read_bytes() == before
        clean()
    for arguments in ((), ("--image", str(image)), ("--image", str(image), "1", "2"),
                      ("--image", str(image), "bad\nentry")):
        before = hashlib.sha256(image.read_bytes()).digest()
        result = call(*arguments, env=env)
        assert result.returncode != 0, result
        assert hashlib.sha256(image.read_bytes()).digest() == before
        clean()
    # --boot-directory is a path INSIDE the selected ext4 filesystem.
    result = call("--image", str(image), "--boot-directory=/boot", "--dry-run", "0", env=env)
    assert result.returncode == 0, result.stdout + result.stderr
    clean()
    result = call("--image", str(image), "--boot-directory", "/missing", "0", env=env)
    assert result.returncode != 0
    clean()
    # Listing remains available without grubenv and cannot write any files.
    subprocess.run([debug, "-w", "-R", "rm /boot/grub/grubenv", str(image)], check=True, capture_output=True)
    before = image.read_bytes()
    result = call("--image", str(image), "--show-default", answer="", env=env)
    assert result.returncode != 0 and image.read_bytes() == before
    clean()
    result = call("--image", str(image), "--list-entries", answer="", env=env)
    assert result.returncode == 0 and "OpenCore" in result.stdout, result
    assert image.read_bytes() == before
    clean()
    for arguments in (("--list-entries", "2"), ("--list-entries", "--dry-run"),
                      ("--list-entries", "--scan"), ("--list-entries", "--boot-directory", "/missing")):
        result = call("--image", str(image), *arguments, answer="", env=env)
        assert result.returncode != 0
        assert image.read_bytes() == before
        clean()
    malformed = base / "malformed.cfg"
    malformed.write_text("menuentry 'Broken' {\n")
    subprocess.run([debug, "-w", "-R", "rm /boot/grub/grub.cfg", str(image)], check=True, capture_output=True)
    subprocess.run([debug, "-w", "-R", f"write {malformed} /boot/grub/grub.cfg", str(image)], check=True, capture_output=True)
    before = image.read_bytes()
    result = call("--image", str(image), "--list-entries", answer="", env=env)
    assert result.returncode != 0 and "listing may be incomplete" in result.stderr, result
    assert image.read_bytes() == before
    clean()
print("Passed CLI image tests: next-boot selection and GNU grub-reboot compatibility, dry-run confirmation, cancellation/EOF, explicit entries, "
      "idempotence, GNU compatibility, preserved bytes, cleanup, read-only menu listing "
      "without grubenv, read-only stored defaults without grub.cfg, separate-boot paths, "
      "and malformed config/argument rejection.")
