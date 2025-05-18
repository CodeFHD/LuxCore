@echo off

REM SPDX-FileCopyrightText: 2024-2025 Authors (see AUTHORS.txt)
REM
REM SPDX-License-Identifier: Apache-2.0


REM Convenience wrapper for CMake commands

REM Script command (1st parameter)
set COMMAND=%1


if "%LUX_PYTHON%" == "" (
    set LUX_PYTHON=python
)

set LUXMAKE="%LUX_PYTHON%" -u -m build-system.luxmake

if "%COMMAND%" == "" (
    call :Config
    call :BuildAndInstall luxcore
    call :BuildAndInstall luxcoreui
    call :BuildAndInstall luxcoreconsole
    call :BuildAndInstall pyluxcore
) else if "%COMMAND%" == "luxcore" (
    call :Config
    call :BuildAndInstall luxcore
) else if "%COMMAND%" == "pyluxcore" (
    call :Config
    call :BuildAndInstall luxcore
    call :BuildAndInstall pyluxcore
) else if "%COMMAND%" == "luxcoreui" (
    call :Config
    call :BuildAndInstall luxcore
    call :BuildAndInstall luxcoreui
) else if "%COMMAND%" == "luxcoreconsole" (
    call :Config
    call :BuildAndInstall luxcore
    call :BuildAndInstall luxcoreconsole
) else if "%COMMAND%" == "doc" (
    call :Config
    call :BuildAndInstall doc
) else if "%COMMAND%" == "config" (
    call :Config
) else if "%COMMAND%" == "package" (
    call :BuildAndInstall package
) else if "%COMMAND%" == "install" (
    call :Install
) else if "%COMMAND%" == "clean" (
    call :Clean
) else if "%COMMAND%" == "clear" (
    call :Clear
) else if "%COMMAND%" == "deps" (
    call :Deps
) else if "%COMMAND%" == "win-recompose" (
    call :WinRecompose %2
) else if "%COMMAND%" == "list-presets" (
    call :ListPresets
) else if "%COMMAND%" == "wheel-test" (
    call :WheelTest
) else (
    echo Command "%COMMAND%" unknown
)
exit /B

:Deps
call %LUXMAKE% deps
goto :EOF

:ListPresets
call %LUXMAKE% list-presets
goto :EOF

:Config
call %LUXMAKE% config
goto :EOF

:BuildAndInstall
call %LUXMAKE% build-and-install %1
goto :EOF

:Install
IF "%~1" == "" (
    %LUXMAKE% all
) else (
    %LUXMAKE% %1
)
goto :EOF

:WinRecompose
call %LUXMAKE% win-recompose %1
goto :EOF

:Clean
call %LUXMAKE% clean
goto :EOF

:WheelTest
call %LUXMAKE% pyluxcore
call %LUXMAKE% wheel-test
goto :EOF

:Clear
REM rmdir /S /Q
call %LUXMAKE% clear
goto :EOF

:EOF
