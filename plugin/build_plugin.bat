@echo off
setlocal
cd /d "%~dp0"

if not exist ..\bin mkdir ..\bin

echo ========================================================
echo   Building TrafficMonitor Rapoo Plugin (rapoo-plugin.dll)
echo ========================================================

where g++ >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [Toolchain] Using MinGW GCC/G++ 64-bit

    windres --codepage 65001 -i plugin.rc -o plugin.o -O coff
    if %ERRORLEVEL% neq 0 (
        echo [ERROR] windres failed!
        exit /b 1
    )
    g++ -shared -O3 -mwindows -municode -s -static rapoo_plugin.cpp plugin.o -lsetupapi -lhid -luser32 -lgdi32 -lshell32 -ladvapi32 -o ..\bin\rapoo-plugin.dll
    if %ERRORLEVEL% neq 0 (
        echo [ERROR] g++ compilation failed!
        exit /b 1
    )
    if exist plugin.o del plugin.o
    goto done
)

echo [ERROR] g++ not found in PATH!
exit /b 1

:done
if exist ..\bin\rapoo-plugin.dll (
    echo.
    echo [SUCCESS] Plugin DLL built successfully: bin\rapoo-plugin.dll
    dir ..\bin\rapoo-plugin.dll | findstr rapoo-plugin.dll
) else (
    echo.
    echo [ERROR] DLL not found!
    exit /b 1
)
