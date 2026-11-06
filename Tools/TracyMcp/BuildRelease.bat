@echo off
setlocal DisableDelayedExpansion
call "%~dp0..\RunPython.bat" "%~dp0BuildRelease.py" %*
exit /b %errorlevel%
