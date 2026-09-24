@echo off
chcp 65001 >nul
title 雷柏鼠标 PID 提取工具
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\get_mouse_pid.ps1"
echo.
pause
