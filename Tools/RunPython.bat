@echo off
setlocal DisableDelayedExpansion
rem 统一 Python 及其子进程的文本编码.
set "PYTHONUTF8=1"
rem 解释器选择顺序：显式环境变量、仓库便携版本、PATH 中的 python.
rem URSA_PYTHON 只填写可执行文件路径，不带外围引号或额外参数.
if defined URSA_PYTHON goto selected
if exist "%~dp0Python\3.11.17\python.exe" (
    set "URSA_PYTHON=%~dp0Python\3.11.17\python.exe"
    goto selected
)
set "URSA_PYTHON=python"
:selected
rem 先检查 Python 版本，避免旧解释器执行后续工具时才报错.
"%URSA_PYTHON%" -c "import sys; sys.exit(0 if sys.version_info >= (3, 11) else 1)" >nul 2>nul
if errorlevel 1 (
    echo Error: Python 3.11+ is required. Set URSA_PYTHON to its executable path.
    exit /b 1
)
rem 转发脚本路径和全部参数，并将执行结果返回给外层入口.
"%URSA_PYTHON%" %*
exit /b %errorlevel%
