# SPDX-FileCopyrightText: 2025 Authors (see AUTHORS.txt)
#
# SPDX-License-Identifier: Apache-2.0

"""Config command."""

from .constants import INSTALL_DIR, SOURCE_DIR
from .utils import run_cmake


def config(
    _,
):
    """CMake config."""
    cmd = [
        "--preset conan-default",
        f"-DCMAKE_INSTALL_PREFIX={str(INSTALL_DIR)}",
        f"-S {str(SOURCE_DIR)}",
    ]
    run_cmake(cmd)
