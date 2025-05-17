# SPDX-FileCopyrightText: 2025 Authors (see AUTHORS.txt)
#
# SPDX-License-Identifier: Apache-2.0

"""Build and install commands."""

import sys
from .constants import INSTALL_DIR, BUILD_TYPE, BUILD_DIR
from .utils import run_cmake, logger
from .presets import get_presets, PresetType


def build(
    args,
):
    """CMake build."""
    preset = _get_preset_from_build_type(BUILD_TYPE)
    cmd = [
        "--build",
        f"--preset {preset}",
        f"--target {args.target}",
    ]
    run_cmake(cmd)


def install(
    args,
):
    """CMake install."""
    cmd = [
        "--install",
        str(BUILD_DIR),
        f"--prefix {INSTALL_DIR}",
        f"--config {BUILD_TYPE}",
        f"--component {args.target}",
    ]
    run_cmake(cmd)


def build_and_install(
    args,
):
    """CMake build and install."""
    build(args)
    install(args)


_PRESETS = {
    "Release": "conan-release",
    "Debug": "conan-debug",
    "RelWithDebInfo": "conan-relwithdebinfo",
    "MinSizeRel": "conan-minsizerel",
}


def _get_preset_from_build_type(
    build_type,
):
    """Get conan preset from build type."""
    try:
        preset = _PRESETS[build_type]
    except KeyError:
        logger.error(
            "Unknown build type '%s'",
            build_type,
        )
        logger.error(
            "Valid values (case sensitive) are: %s",
            _PRESETS.keys(),
        )
        sys.exit(1)
    if preset not in (presets := get_presets(PresetType.BUILD)):
        logger.error(
            "Preset '%s' missing",
            preset,
        )
        logger.error(
            "Available presets: %s",
            presets,
        )
    return preset
