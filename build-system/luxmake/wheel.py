# SPDX-FileCopyrightText: 2025 Authors (see AUTHORS.txt)
#
# SPDX-License-Identifier: Apache-2.0

"""Make wheel command."""

import sys
import json
import subprocess
import re
import tempfile
import shutil
import platform
from pathlib import Path

from .constants import SOURCE_DIR, INSTALL_DIR, WHEEL_DIR, WHEEL_LIB_DIR
from .utils import logger
from .build import build_and_install


def _get_glibc_version():
    """
    Returns the version of glibc installed on the system

    None if it cannot be determined.
    """
    # Run ldd --version and capture the output
    result = subprocess.run(
        ["ldd", "--version"], capture_output=True, text=True, check=True
    )
    output = result.stdout or result.stderr
    # Look for a version number in the output
    if (match := re.search(r"(\d+\.\d+(?:\.\d+)*)", output)):
        return match.group(1)
    return None


def make_wheel(args):
    """Build a wheel."""
    logger.warning(
        "This command builds a TEST wheel, "
        "not fully compliant to standard "
        "and only intended for test. "
        "DO NOT USE IN PRODUCTION."
    )
    # Build and install pyluxcore
    args.target = "pyluxcore"
    build_and_install(args)

    # Compute version
    build_settings_file = Path("build-system", "build-settings.json")
    with open(build_settings_file, encoding="utf-8") as in_file:
        default_version = json.load(in_file)["DefaultVersion"]
    version = ".".join(default_version[i] for i in ("major", "minor", "patch"))

    # Compute tag
    vinfo = sys.version_info
    python_tag = f"cp{vinfo.major}{vinfo.minor}"
    abi_tag = python_tag
    glibc_version = _get_glibc_version().replace(".", "_")
    platform_tag = f"manylinux_{glibc_version}_x86_64"
    platform_tag = "linux_x86_64"  # TODO
    # TODO Only Linux at the moment
    tag = f"{python_tag}-{abi_tag}-{platform_tag}"

    # Destination folder
    logger.info("Making wheel for version '%s' and tag '%s'", version, tag)
    with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as tmp_out:
        tmp = Path(tmp)
        tmp_out = Path(tmp_out)

        # Create dist-info
        dist_info = tmp / f"pyluxcore-{version}.dist-info"
        dist_info.mkdir(exist_ok=True)

        # Compute tag
        # This tag is absolutely mendacious. Do not use in production.
        # https://packaging.python.org/en/latest/specifications/platform-compatibility-tags/
        # Export WHEEL file
        wheel_content = f"""\
Wheel-Version: 1.0
Generator: fake 0.0.0
Root-Is-Purelib: false
Tag: {tag}
"""
        with open(dist_info / "WHEEL", "w", encoding="utf-8") as f:
            f.write(wheel_content)

        # Copy subfolders
        shutil.copytree(
            SOURCE_DIR / "python" / "pyluxcore",
            tmp / "pyluxcore",
            dirs_exist_ok=True,
        )
        shutil.copytree(
            INSTALL_DIR / "pyluxcore", tmp / "pyluxcore", dirs_exist_ok=True
        )
        shutil.copytree(
            INSTALL_DIR / "pyluxcore.libs",
            tmp / "pyluxcore.libs",
            dirs_exist_ok=True,
        )

        # Pack wheel
        pack_cmd = ["wheel", "pack", tmp, "--dest-dir", str(tmp_out)]
        subprocess.run(pack_cmd, text=True, check=True)

        # Then repair
        # TODO
        logger.info("Repairing wheel")
        if platform.system() != "Linux":
            cmd = [
                "repairwheel",
                "-o",
                str(WHEEL_DIR),
                "-l",
                str(WHEEL_LIB_DIR),
                str(tmp_out / f"pyluxcore-{version}-{tag}.whl"),
            ]
        else:
            cmd = [
                "pipx",
                "run",
                "auditwheel",
                "repair",
                "--plat",
                platform_tag,
                "--wheel-dir",
                str(INSTALL_DIR / "wheel"),
                str(tmp_out / f"pyluxcore-{version}-{tag}.whl"),
            ]
        # TODO repairwheel to be installed (via pipx?) ?
        # Or per-platform approach?
        try:
            result = subprocess.check_output(cmd, text=True)
        except subprocess.CalledProcessError as err:
            logger.error(err)
            sys.exit(1)
        logger.info(result)
