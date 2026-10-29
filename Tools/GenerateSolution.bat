@echo off
setlocal DisableDelayedExpansion
call "%~dp0RunPython.bat" "%~dp0GenerateSolution.py" %*
exit /b %errorlevel%
