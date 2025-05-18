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
import sys

from wheel.wheelfile import WheelFile

from .utils import pack, unpack, logger

def _rename(basepath, org, dest):
    """Rename wheel component."""
    try:
        shutil.move(
            basepath / "pyluxcore.libs" / org,
            basepath / "pyluxcore.libs" / dest,
        )
    except FileNotFoundError:
        logger.error("Missing file in wheel: '%s'", org)
        sys.exit(1)


def win_recompose(args):
    """Recompose Windows wheel (see module docstring)."""
    wheel_path = Path(args.wheel)
    wheel_folder = wheel_path.parents[0]

    logger.info("Recomposing '%s'", wheel_path)

    with tempfile.TemporaryDirectory() as tmpdir:  # Working space
        # Unpack wheel
        unpack(path=wheel_path, dest=tmpdir)
        with WheelFile(wheel_path) as wheel_file:
            namever = wheel_file.parsed_filename.group("namever")
            unpacked_wheel_path = Path(tmpdir) / namever

        # Rename and move oidnDenoise
        logger.info("Renaming oidnDenoise.pyd into oidnDenoise.exe")
        _rename(unpacked_wheel_path, "oidnDenoise.pyd", "oidnDenoise.exe")

        # Rename and move OpenImageDenoise_device_cpu
        logger.info(
            "Renaming OpenImageDenoise_device_cpu.pyd "
            "into OpenImageDenoise_device_cpu.dll"
        )
        _rename(
            unpacked_wheel_path,
            "OpenImageDenoise_device_cpu.pyd",
            "OpenImageDenoise_device_cpu.dll",
        )

        # Repack wheel
        pack(
            directory=unpacked_wheel_path,
            dest_dir=wheel_folder,
        )


if __name__ == "__main__":
    # Parse arguments
    parser = argparse.ArgumentParser()
    parser.add_argument("wheelpath", type=Path)
    args = parser.parse_args()

    recompose(args.wheelpath)
