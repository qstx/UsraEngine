@echo off
setlocal DisableDelayedExpansion
set "PYTHONUTF8=1"
"%~dp0..\..\..\..\Python\3.11.17\python.exe" "%~dp0Start-TracyMcp.py" %*
exit /b %errorlevel%
