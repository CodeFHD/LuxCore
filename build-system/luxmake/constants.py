# SPDX-FileCopyrightText: 2025 Authors (see AUTHORS.txt)
#
# SPDX-License-Identifier: Apache-2.0

"""Constants for luxmake."""

import os
import pathlib

# External variables
BINARY_DIR = pathlib.Path(
    os.getenv(
        "LUX_BINARY_DIR",
        "out",
    )
)
SOURCE_DIR = pathlib.Path(
    os.getenv(
        "LUX_SOURCE_DIR",
        os.getcwd(),
    )
)
BUILD_TYPE = os.getenv(
    "LUX_BUILD_TYPE",
    "Release",
)

# Computed variables
BUILD_DIR = BINARY_DIR / "build"
INSTALL_DIR = BINARY_DIR / "install" / BUILD_TYPE
WHEEL_BUILD_DIR = BUILD_DIR / "wheel"
WHEEL_LIB_DIR = INSTALL_DIR / "lib"  # TODO Windows, MacOS etc.
WHEEL_DIR = BINARY_DIR / "wheel"
