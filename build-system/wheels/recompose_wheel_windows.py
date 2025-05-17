# SPDX-FileCopyrightText: 2024 Howetuft
#
# SPDX-License-Identifier: Apache-2.0

"""This script makes a final rearrangement to a Windows wheel.

2 actions to be done:
- Rename and move oidnDenoise.exe
- Rename and move OpenImageDenoise_device_cpu.dll
"""

from pathlib import Path
import argparse
import tempfile
import shutil
import subprocess
import sys

from wheel.wheelfile import WheelFile


def unpack(path, dest):
    """Unpack a wheel."""
    args = [
        sys.executable,
        "-m",
        "wheel",
        "unpack",
        f"--dest={dest}",
        str(path),
    ]
    try:
        output = subprocess.check_output(
            args, text=True, stderr=subprocess.STDOUT
        )
    except subprocess.CalledProcessError as err:
        print(err.output)
        raise
    print(output)


def pack(directory, dest_dir):
    """(Re)pack a wheel."""
    args = [
        sys.executable,
        "-m",
        "wheel",
        "pack",
        f"--dest-dir={dest_dir}",
        str(directory),
    ]
    try:
        output = subprocess.check_output(
            args, text=True, stderr=subprocess.STDOUT
        )
    except subprocess.CalledProcessError as err:
        print(err.output)
        raise
    print(output)


def main():
    """Main entry point."""
    # Parse arguments
    parser = argparse.ArgumentParser()
    parser.add_argument("wheelpath", type=Path)
    args = parser.parse_args()

    wheel_path = args.wheelpath
    wheel_folder = wheel_path.parents[0]

    print(f"Recomposing {wheel_path}")

    with tempfile.TemporaryDirectory() as tmpdir:  # Working space
        # Unpack wheel
        unpack(path=args.wheelpath, dest=tmpdir)
        with WheelFile(args.wheelpath) as wheelfile:
            namever = wheelfile.parsed_filename.group("namever")
            unpacked_wheel_path = Path(tmpdir) / namever

        # Rename and move oidnDenoise
        print("Rename oidnDenoise.pyd into oidnDenois.exe")
        shutil.move(
            unpacked_wheel_path / "pyluxcore.libs" / "oidnDenoise.pyd",
            unpacked_wheel_path / "pyluxcore.libs" / "oidnDenoise.exe",
        )

        # Rename and move OpenImageDenoise_device_cpu
        print(
            "Rename OpenImageDenoise_device_cpu.pyd "
            "into OpenImageDenoise_device_cpu.dll"
        )
        shutil.move(
            unpacked_wheel_path
            / "pyluxcore.libs"
            / "OpenImageDenoise_device_cpu.pyd",
            unpacked_wheel_path
            / "pyluxcore.libs"
            / "OpenImageDenoise_device_cpu.dll",
        )

        # Repack wheel
        pack(
            directory=unpacked_wheel_path,
            dest_dir=wheel_folder,
        )


if __name__ == "__main__":
    main()
