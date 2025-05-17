# SPDX-FileCopyrightText: 2025 Authors (see AUTHORS.txt)
#
# SPDX-License-Identifier: Apache-2.0

"""Clear and clean commands."""

import shutil

from .constants import BINARY_DIR
from .utils import logger
from .presets import get_presets, PresetType
from .utils import run_cmake


def clean(
    _,
):
    """CMake clean."""
    for preset in get_presets(PresetType.BUILD):
        logger.info(
            "Cleaning preset '%s'",
            preset,
        )
        cmd = [
            "--build",
            f"--preset {preset}",
            "--target clean",
        ]
        run_cmake(cmd)


def clear(
    _,
):
    """Clear binary directory."""
    # We just remove the subdirectories, in order to avoid
    # unwanted removals if BINARY_DIR points to a wrong directory
    for subdir in (
        "build",
        "dependencies",
        "install",
    ):
        directory = BINARY_DIR / subdir
        logger.info(
            "Removing '%s'",
            directory,
        )
        try:
            shutil.rmtree(
                directory,
                ignore_errors=True,
            )
        except FileNotFoundError:
            logger.debug(
                "'%s' not found",
                directory,
            )
