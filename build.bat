@echo off
setlocal
cd /d "%~dp0"

if not exist bin mkdir bin

echo ========================================================
echo   Building rapoo-tray (Release Standalone Executable)
echo ========================================================

where g++ >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [Toolchain] Using MinGW GCC/G++ ...
    windres --codepage 65001 -I res -i res\app.rc -o bin\app.o
    g++ -O3 -mwindows -municode -s -static src\main.cpp src\rapoo_protocol.cpp src\device_manager.cpp src\osd_window.cpp src\tray_menu.cpp src\alert_window.cpp bin\app.o -lsetupapi -lhid -luser32 -lgdi32 -lshell32 -ladvapi32 -luxtheme -ldwmapi -o bin\rapoo-tray.exe
    if exist bin\app.o del bin\app.o
    goto done
)

where cl >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [Toolchain] Using MSVC CL ...
    rc /c 65001 /fo bin\app.res res\app.rc
    cl /nologo /O2 /MT /utf-8 /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX src\main.cpp src\rapoo_protocol.cpp src\device_manager.cpp src\osd_window.cpp src\tray_menu.cpp src\alert_window.cpp bin\app.res /Fe:bin\rapoo-tray.exe /link /SUBSYSTEM:WINDOWS setupapi.lib hid.lib user32.lib gdi32.lib shell32.lib advapi32.lib uxtheme.lib dwmapi.lib
    if exist bin\app.res del bin\app.res
    if exist *.obj del *.obj
    goto done
)

where clang++ >nul 2>nul
if %ERRORLEVEL% equ 0 (
    set "CLANGXX=clang++"
)
if not defined CLANGXX if exist "D:\VS2022\tool\bin\clang++.exe" (
    set "CLANGXX=D:\VS2022\tool\bin\clang++.exe"
)
if defined CLANGXX if exist "D:\VS2022\tool\bin\llvm-rc.exe" (
    echo [Toolchain] Using LLVM Clang++ ...
    "D:\VS2022\tool\bin\llvm-rc.exe" /fo bin\app.res res\app.rc
    "%CLANGXX%" -O3 src\main.cpp src\rapoo_protocol.cpp src\device_manager.cpp src\osd_window.cpp src\tray_menu.cpp src\alert_window.cpp bin\app.res -lsetupapi -lhid -luser32 -lgdi32 -lshell32 -ladvapi32 -luxtheme -Xlinker /SUBSYSTEM:WINDOWS -o bin\rapoo-tray.exe
    if exist bin\app.res del bin\app.res
    goto done
)

echo [ERROR] Neither g++, cl.exe, nor clang++ was found.
exit /b 1

:done
if exist bin\rapoo-tray.exe (
    echo.
    echo [SUCCESS] Build completed successfully: bin\rapoo-tray.exe
    echo File size:
    dir bin\rapoo-tray.exe | findstr rapoo-tray.exe
) else (
    echo.
    echo [ERROR] Compilation failed.
    exit /b 1
)
