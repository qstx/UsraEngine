@echo off
setlocal DisableDelayedExpansion
rem URSA_PYTHON is an executable path, without surrounding quotes or arguments.
if defined URSA_PYTHON goto selected
if exist "%~dp0Python\3.11.17\python.exe" (
    set "URSA_PYTHON=%~dp0Python\3.11.17\python.exe"
    goto selected
)
set "URSA_PYTHON=python"
:selected
"%URSA_PYTHON%" -c "import sys; sys.exit(0 if sys.version_info >= (3, 11) else 1)" >nul 2>nul
if errorlevel 1 (
    echo Error: Python 3.11+ is required. Set URSA_PYTHON to its executable path.
    exit /b 1
)
"%URSA_PYTHON%" %*
exit /b %errorlevel%
