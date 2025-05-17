# SPDX-FileCopyrightText: 2025 Authors (see AUTHORS.txt)
#
# SPDX-License-Identifier: Apache-2.0

"""Utilities for make wrapper."""

import sys
import functools
import logging
import shutil
import subprocess

# Logger
logger = logging.getLogger("LuxCore Build")


# Cmake
@functools.cache
def ensure_cmake_app():
    """Ensure cmake is installed."""
    logger.debug("Looking for cmake")
    if not (res := shutil.which("cmake")):
        logger.error("CMake not found!")
        sys.exit(1)
    logger.debug(
        "CMake found: '%s'",
        res,
    )
    return res


def run_cmake(
    args,
    **kwargs,
):
    """Run cmake statement."""
    cmake_app = ensure_cmake_app()
    args = [cmake_app] + args
    logger.debug(args)
    res = subprocess.run(
        args,
        shell=False,
        check=False,
        **kwargs,
    )
    if res.returncode:
        logger.error("Error while executing cmake")
        print(res.stdout)
        print(res.stderr)
        sys.exit(1)
    return res
