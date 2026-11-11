@echo off
rem Opens the music converter window (Windows' own PowerShell; nothing to install).
start "" powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0tools\scripts\music_converter.ps1"
