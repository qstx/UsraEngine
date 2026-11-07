@echo off
setlocal DisableDelayedExpansion
rem 通过统一入口选择 Python 解释器，再执行解决方案生成流程.
call "%~dp0RunPython.bat" "%~dp0GenerateSolution.py" %*
rem 保留生成流程的成功或失败状态，供外层 BAT 和自动化使用.
exit /b %errorlevel%
