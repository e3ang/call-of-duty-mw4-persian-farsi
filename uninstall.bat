@echo off
title MW4 Farsi - Uninstall
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\installer.ps1" -Mode uninstall
