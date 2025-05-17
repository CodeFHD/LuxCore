# SPDX-FileCopyrightText: 2025 Authors (see AUTHORS.txt)
#
# SPDX-License-Identifier: Apache-2.0

"""This script downloads and installs dependencies for LuxCore build."""

import os
import tempfile
from urllib.request import (
    urlretrieve,
)
from urllib.parse import (
    urlparse,
)
from pathlib import (
    Path,
)
from zipfile import (
    ZipFile,
)
import subprocess
import logging
import shutil
import argparse
import json
import platform
import sys
from functools import (
    cache,
)
from dataclasses import (
    dataclass,
)


from .constants import BINARY_DIR

CONAN_ALL_PACKAGES = '"*"'


logger = logging.getLogger("LuxCore Dependencies")

CONAN_ENV = {}

URL_SUFFIXES = {
    "Linux-X64": "ubuntu-latest",
    "Windows-X64": "windows-latest",
    "macOS-ARM64": "macos-14",
    "macOS-X64": "macos-13",
}


@dataclass
class Colors:
    """Colors for terminal output."""

    HEADER = "\033[95m"
    OKBLUE = "\033[94m"
    OKCYAN = "\033[96m"
    OKGREEN = "\033[92m"
    WARNING = "\033[93m"
    FAIL = "\033[91m"
    ENDC = "\033[0m"
    BOLD = "\033[1m"
    UNDERLINE = "\033[4m"


def find_platform():
    """Find current platform."""
    system = platform.system()
    if system == "Linux":
        res = "Linux-X64"
    elif system == "Windows":
        res = "Windows-X64"
    elif system == "Darwin":
        machine = platform.machine()
        if machine == "arm64":
            res = "macOS-ARM64"
        elif machine == "x86_64":
            res = "macOS-X64"
        else:
            raise RuntimeError(f"Unknown machine for MacOS: '{machine}'")
    else:
        raise RuntimeError(f"Unknown system '{system}'")
    return res


def build_url(
    user,
    release,
):
    """Build the url to download from."""
    suffix = URL_SUFFIXES[find_platform()]

    if not user:
        user = "LuxCoreRender"

    url = (
        "https://github.com",
        user,
        "LuxCoreDeps",
        "releases",
        "download",
        f"v{release}",
        f"luxcore-deps-{suffix}.zip",
    )

    return "/".join(url)


def get_profile_name():
    """Get the profile file name, based on platform."""
    return f"conan-profile-{find_platform()}"


@cache
def ensure_conan_app():
    """Ensure Conan is installed."""
    logger.info("Looking for conan")
    if not (res := shutil.which("conan")):
        logger.error("Conan not found!")
        sys.exit(1)
    logger.info(
        "Conan found: '%s'",
        res,
    )
    return res


def run_conan(
    args,
    **kwargs,
):
    """Run conan statement."""
    conan_app = ensure_conan_app()
    if "env" not in kwargs:
        kwargs["env"] = CONAN_ENV
    else:
        kwargs["env"].update(CONAN_ENV)
    kwargs["env"].update(os.environ)
    kwargs["text"] = kwargs.get(
        "text",
        True,
    )
    args = [conan_app] + args
    logger.debug(args)
    res = subprocess.run(
        args,
        shell=False,
        check=False,
        **kwargs,
    )
    if res.returncode:
        logger.error("Error while executing conan")
        print(res.stdout)
        print(res.stderr)
        sys.exit(1)
    return res


def download(
    url,
    destdir,
):
    """Download file from url into destdir."""
    # Download artifact
    destdir = Path(destdir)
    filename = urlparse(url).path.split("/")[-1]
    filepath = destdir / filename
    (
        local_filename,
        _,
    ) = urlretrieve(
        url,
        filename=filepath,
    )

    # Check attestation
    logger.info(
        "Checking '%s'",
        local_filename,
    )

    if not (gh_app := shutil.which("gh")):
        msg = (
            Colors.WARNING,
            "SIGNATURE CHECKING ERROR",
            Colors.ENDC,
        )
        msg = "".join(msg)
        logger.warning(msg)
        msg = (
            Colors.WARNING,
            "Cannot find 'gh' application - ",
            "Dependencies origin cannot be checked.",
            Colors.ENDC,
        )
        msg = "".join(msg)
        logger.warning(msg)
    else:
        gh_cmd = [
            gh_app,
            "attestation",
            "verify",
            "-oLuxCoreRender",
            "--format",
            "json",
            filepath,
        ]
        errormsg = f"{Colors.WARNING}SIGNATURE CHECKING ERROR{Colors.ENDC}"
        try:
            gh_output = subprocess.check_output(
                gh_cmd,
                text=True,
            )
        except subprocess.CalledProcessError as err:
            logger.warning(errormsg)
            logger.warning(
                "gh return code: %s",
                err.returncode,
            )
            logger.warning(err.output)
        except OSError as err:
            logger.warning(errormsg)
            logger.warning(str(err))
        else:
            msg = f"{Colors.OKGREEN}'%s': found certificate - OK{Colors.ENDC}"
            logger.info(
                msg,
                filename,
            )
            (
                signature,
                *_,
            ) = json.loads(gh_output)
            certificate = signature["verificationResult"]["signature"][
                "certificate"
            ]
            logger.debug(
                json.dumps(
                    certificate,
                    indent=2,
                )
            )

    # Unzip
    with ZipFile(local_filename) as downloaded:
        downloaded.extractall(destdir)


def install(
    filename,
    destdir,
):
    """Install file from local directory into destdir."""
    logger.info(
        "Importing %s",
        filename,
    )
    with ZipFile(str(filename)) as zipfile:
        zipfile.extractall(destdir)


def conan_home():
    """Get Conan home path."""
    conan_app = ensure_conan_app()
    res = subprocess.run(
        [
            conan_app,
            "config",
            "home",
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if res.returncode:
        logger.error("Error while executing conan")
        print(res.stdout)
        print(res.stderr)
        sys.exit(1)
    return Path(res.stdout.strip())


def copy_conf(
    dest,
):
    """Copy global.conf into conan tree."""
    home = conan_home()
    source = home / "global.conf"
    logger.info(
        "Copying %s to %s",
        source,
        dest,
    )
    shutil.copy(
        source,
        dest,
    )


def main(
    call_args=None,
):
    """Entry point."""
    output_dir = os.getenv(
        "output_dir",
        "out",
    )

    # Set-up logger
    logger.setLevel(logging.INFO)
    logging.basicConfig(level=logging.INFO)
    msg = f"{Colors.OKBLUE}BEGIN{Colors.ENDC}"
    logger.info(msg)

    # Get settings
    logger.info("Reading settings")
    with open(
        "build-system/build-settings.json",
        encoding="utf-8",
    ) as f:
        settings = json.load(f)
    logger.info(
        "Output directory: %s",
        output_dir,
    )

    # Get optional command-line parameters
    # Nota: --local option is used by LuxCoreDeps CI
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-l",
        "--local",
        type=Path,
        help="Use local dependency set (debug)",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="Output directory",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Print additional information",
    )
    parser.add_argument(
        "-e",
        "--extended",
        action="store_true",
        help="Extended presets (including RelWithDebInfo & MinSizeRel)",
    )
    if call_args is None:
        args = parser.parse_args()  # Parse command line
    else:
        args = parser.parse_args(call_args)
    if args.verbose:
        logger.setLevel(logging.DEBUG)
    if args.output:
        output_dir = args.output

    # Process
    with tempfile.TemporaryDirectory() as tmpdir:

        tmpdir = Path(tmpdir)

        _conan_home = tmpdir / ".conan2"

        CONAN_ENV.update(
            {
                "CONAN_HOME": str(_conan_home),
                "GCC_VERSION": str(settings["Build"]["gcc"]),
                "CXX_VERSION": str(settings["Build"]["cxx"]),
                "BUILD_TYPE": "Release",  # TODO Command line parameter
            }
        )

        # Initialize
        user = settings["Dependencies"]["user"]
        release = settings["Dependencies"]["release"]
        url = build_url(
            user,
            release,
        )

        # Download and unzip
        if not args.local:
            logger.info(
                "Downloading dependencies (url='%s')",
                url,
            )
            download(
                url,
                tmpdir,
            )
        else:
            logger.info(
                "Using local dependency set ('%s')",
                args.local,
            )

        # Clean
        logger.info("Cleaning local cache")
        res = run_conan(
            [
                "remove",
                "-c",
                "*",
            ],
            capture_output=True,
        )
        for line in res.stderr.splitlines():
            logger.info(line)
        copy_conf(_conan_home)  # Copy global.conf in current conan home

        # Install
        logger.info("Installing")
        if not (local := args.local):
            archive = tmpdir / "conan-cache-save.tgz"
        else:
            archive = local
        res = run_conan(
            [
                "cache",
                "restore",
                archive,
            ],
            capture_output=True,
        )
        for line in res.stderr.splitlines():
            logger.info(line)

        # Check
        logger.info("Checking integrity")
        res = run_conan(
            [
                "cache",
                "check-integrity",
                "*",
            ],
            capture_output=True,
        )
        logger.info("Integrity check: OK")

        # Installing profiles
        logger.info("Installing profiles")
        run_conan(
            [
                "config",
                "install-pkg",
                f"luxcoreconf/{release}@luxcore/luxcore",
            ]
        )

        # Generate & deploy
        # About release/debug mixing, see
        # https://github.com/conan-io/conan/issues/12656
        logger.info("Generating...")
        generator = "Ninja Multi-Config"
        main_block = [
            "install",
            "--build=missing",
            f"--profile:all={get_profile_name()}",
            "--deployer=full_deploy",
            f"--deployer-folder={output_dir}/dependencies",
            f"--output-folder={output_dir}",
            "--settings=build_type=Release",
            f"--conf:all=tools.cmake.cmaketoolchain:generator={generator}",
        ]
        if not os.getenv("DEPS_WITHOUT_SUDO"):
            main_block += [
                "--conf:all=tools.system.package_manager:sudo=true",
            ]
        else:
            logger.info("Sudo deactivated for package manager")
        build_types = [
            "Debug",
            "Release",
        ]
        if args.extended:
            build_types += [
                "RelWithDebInfo",
                "MinSizeRel",
            ]
        for build_type in build_types:
            logger.info(
                "Generating '%s'",
                build_type,
            )
            end_block = [
                f"--settings=&:build_type={build_type}",
                Path(
                    "build-system",
                    "conan",
                    "conanfile.py",
                ),
            ]
            run_conan(main_block + end_block)

        # Show presets
        subprocess.run(
            [
                "cmake",
                "--list-presets=build",
            ],
            check=False,
        )
        print(
            "",
            flush=True,
        )

    msg = Colors.OKBLUE + "END" + Colors.ENDC
    logger.info(msg)


def deps(
    _,
):
    """Install dependencies."""
    main([f"--output={BINARY_DIR}"])


if __name__ == "__main__":
    main()
