@echo off
title SC64 Launcher - Get covers and game info
rem Runs the covers tool with the Python that comes in the tools folder (nothing to install).
"%~dp0tools\python\python.exe" "%~dp0tools\scripts\get_covers.py"
echo.
pause
