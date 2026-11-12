@echo off
rem Opens the "Get covers and game info" window (Windows' own PowerShell; nothing to install).
start "" powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0tools\scripts\covers_window.ps1"
