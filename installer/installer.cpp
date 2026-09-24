#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <strsafe.h>
#include <stdbool.h>
#include <stdio.h>
#include <tlhelp32.h>

#define IDR_APP_PAYLOAD      1001

#define IDC_BTN_INSTALL      2001
#define IDC_BTN_CANCEL       2002
#define IDC_CHK_AUTORUN      2003
#define IDC_CHK_DESKTOP      2004
#define IDC_CHK_LAUNCH       2005

static HINSTANCE g_hInstance = NULL;
static HWND g_hMainWnd = NULL;
static HWND g_hChkAutoRun = NULL;
static HWND g_hChkDesktop = NULL;
static HWND g_hChkLaunch = NULL;
static HWND g_hBtnInstall = NULL;
static HWND g_hBtnCancel = NULL;
static HFONT g_hFontTitle = NULL;
static HFONT g_hFontBody = NULL;

static const WCHAR* REG_RUN = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const WCHAR* REG_UNINSTALL = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\rapoo-tray";
static const WCHAR* APP_NAME = L"rapoo-tray";

// Kill running process by executable name
static void TerminateAppProcess() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"rapoo-tray.exe") == 0 || _wcsicmp(pe.szExeFile, L"RapooVT7Tray.exe") == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    TerminateProcess(hProc, 0);
                    WaitForSingleObject(hProc, 1000);
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    Sleep(150);
}

// Create Shell Shortcut (.lnk)
static bool CreateShellShortcut(LPCWSTR targetPath, LPCWSTR lnkPath, LPCWSTR description, LPCWSTR iconPath) {
    IShellLinkW* psl = NULL;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&psl);
    if (FAILED(hr)) return false;

    psl->SetPath(targetPath);
    psl->SetDescription(description);
    if (iconPath && iconPath[0] != L'\0') {
        psl->SetIconLocation(iconPath, 0);
    }

    IPersistFile* ppf = NULL;
    hr = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
    if (SUCCEEDED(hr)) {
        hr = ppf->Save(lnkPath, TRUE);
        ppf->Release();
    }
    psl->Release();
    return SUCCEEDED(hr);
}

// Extract payload resource to file
static bool ExtractPayloadToFile(LPCWSTR destPath) {
    HRSRC hRes = FindResourceW(g_hInstance, MAKEINTRESOURCEW(IDR_APP_PAYLOAD), RT_RCDATA);
    if (!hRes) return false;

    HGLOBAL hMem = LoadResource(g_hInstance, hRes);
    if (!hMem) return false;

    DWORD size = SizeofResource(g_hInstance, hRes);
    LPVOID pData = LockResource(hMem);
    if (!pData || size == 0) return false;

    HANDLE hFile = CreateFileW(destPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(hFile, pData, size, &written, NULL);
    CloseHandle(hFile);
    return (ok && written == size);
}

// Get installation target directory: %LOCALAPPDATA%\Programs\rapoo-tray
static bool GetInstallDir(WCHAR* outDir, DWORD maxLen) {
    WCHAR localApp[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localApp))) {
        return false;
    }
    StringCchPrintfW(outDir, maxLen, L"%s\\Programs\\rapoo-tray", localApp);
    return true;
}

// Perform Installation
static void DoInstall() {
    EnableWindow(g_hBtnInstall, FALSE);
    EnableWindow(g_hBtnCancel, FALSE);

    // 1. Terminate old running instances
    TerminateAppProcess();

    // 2. Prepare directories
    WCHAR installDir[MAX_PATH];
    if (!GetInstallDir(installDir, MAX_PATH)) {
        MessageBoxW(g_hMainWnd, L"无法获取安装路径。", L"错误", MB_ICONERROR);
        EnableWindow(g_hBtnInstall, TRUE);
        EnableWindow(g_hBtnCancel, TRUE);
        return;
    }
    CreateDirectoryW(installDir, NULL);

    WCHAR appExePath[MAX_PATH];
    StringCchPrintfW(appExePath, MAX_PATH, L"%s\\rapoo-tray.exe", installDir);

    WCHAR uninstallerPath[MAX_PATH];
    StringCchPrintfW(uninstallerPath, MAX_PATH, L"%s\\Uninstall.exe", installDir);

    // 3. Extract rapoo-tray.exe
    if (!ExtractPayloadToFile(appExePath)) {
        MessageBoxW(g_hMainWnd, L"写入文件失败，请确认程序是否正在运行。", L"错误", MB_ICONERROR);
        EnableWindow(g_hBtnInstall, TRUE);
        EnableWindow(g_hBtnCancel, TRUE);
        return;
    }

    // 4. Copy current installer as Uninstall.exe
    WCHAR selfExe[MAX_PATH];
    GetModuleFileNameW(NULL, selfExe, MAX_PATH);
    CopyFileW(selfExe, uninstallerPath, FALSE);

    // 5. Create Start Menu shortcut
    WCHAR startMenuPrograms[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, startMenuPrograms))) {
        WCHAR startLnk[MAX_PATH];
        StringCchPrintfW(startLnk, MAX_PATH, L"%s\\rapoo-tray.lnk", startMenuPrograms);
        CreateShellShortcut(appExePath, startLnk, L"rapoo-tray", appExePath);
    }

    // 6. Create Desktop shortcut
    if (SendMessageW(g_hChkDesktop, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        WCHAR desktopDir[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopDir))) {
            WCHAR deskLnk[MAX_PATH];
            StringCchPrintfW(deskLnk, MAX_PATH, L"%s\\rapoo-tray.lnk", desktopDir);
            CreateShellShortcut(appExePath, deskLnk, L"rapoo-tray", appExePath);
        }
    }

    // 7. Configure Run on Startup
    HKEY hKeyRun;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKeyRun, NULL) == ERROR_SUCCESS) {
        if (SendMessageW(g_hChkAutoRun, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            RegSetValueExW(hKeyRun, APP_NAME, 0, REG_SZ, (const BYTE*)appExePath, (DWORD)((wcslen(appExePath) + 1) * sizeof(WCHAR)));
        } else {
            RegDeleteValueW(hKeyRun, APP_NAME);
        }
        RegCloseKey(hKeyRun);
    }

    // 8. Register in Windows Add/Remove Programs
    HKEY hKeyUn;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_UNINSTALL, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKeyUn, NULL) == ERROR_SUCCESS) {
        const WCHAR* name = L"rapoo-tray";
        const WCHAR* ver = L"1.3.0";
        const WCHAR* pub = L"Iris";
        WCHAR unCmd[MAX_PATH + 32];
        StringCchPrintfW(unCmd, sizeof(unCmd) / sizeof(WCHAR), L"\"%s\" /uninstall", uninstallerPath);

        RegSetValueExW(hKeyUn, L"DisplayName", 0, REG_SZ, (const BYTE*)name, (DWORD)((wcslen(name) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKeyUn, L"DisplayVersion", 0, REG_SZ, (const BYTE*)ver, (DWORD)((wcslen(ver) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKeyUn, L"Publisher", 0, REG_SZ, (const BYTE*)pub, (DWORD)((wcslen(pub) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKeyUn, L"DisplayIcon", 0, REG_SZ, (const BYTE*)appExePath, (DWORD)((wcslen(appExePath) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKeyUn, L"UninstallString", 0, REG_SZ, (const BYTE*)unCmd, (DWORD)((wcslen(unCmd) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKeyUn, L"InstallLocation", 0, REG_SZ, (const BYTE*)installDir, (DWORD)((wcslen(installDir) + 1) * sizeof(WCHAR)));
        RegCloseKey(hKeyUn);
    }

    // 9. Launch application
    if (SendMessageW(g_hChkLaunch, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        ShellExecuteW(NULL, L"open", appExePath, NULL, installDir, SW_SHOWNORMAL);
    }

    MessageBoxW(g_hMainWnd, L"安装完成。", L"提示", MB_ICONINFORMATION | MB_OK);
    DestroyWindow(g_hMainWnd);
}

// Perform Uninstallation
static void DoUninstall() {
    int res = MessageBoxW(
        NULL,
        L"确定要卸载 rapoo-tray 吗？",
        L"卸载确认",
        MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2
    );
    if (res != IDYES) return;

    // 1. Terminate app
    TerminateAppProcess();

    // 2. Remove Shortcuts
    WCHAR startMenuPrograms[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, startMenuPrograms))) {
        WCHAR startLnk[MAX_PATH];
        StringCchPrintfW(startLnk, MAX_PATH, L"%s\\rapoo-tray.lnk", startMenuPrograms);
        DeleteFileW(startLnk);
    }

    WCHAR desktopDir[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopDir))) {
        WCHAR deskLnk[MAX_PATH];
        StringCchPrintfW(deskLnk, MAX_PATH, L"%s\\rapoo-tray.lnk", desktopDir);
        DeleteFileW(deskLnk);
    }

    // 3. Remove Run Key
    HKEY hKeyRun;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, KEY_SET_VALUE, &hKeyRun) == ERROR_SUCCESS) {
        RegDeleteValueW(hKeyRun, APP_NAME);
        RegCloseKey(hKeyRun);
    }

    // 4. Remove Uninstall Key
    RegDeleteKeyW(HKEY_CURRENT_USER, REG_UNINSTALL);

    // 5. Delete files and remove directory
    WCHAR installDir[MAX_PATH];
    if (GetInstallDir(installDir, MAX_PATH)) {
        WCHAR appExe[MAX_PATH];
        StringCchPrintfW(appExe, MAX_PATH, L"%s\\rapoo-tray.exe", installDir);
        DeleteFileW(appExe);

        WCHAR cmd[MAX_PATH * 3];
        StringCchPrintfW(cmd, sizeof(cmd) / sizeof(WCHAR),
            L"/c timeout /t 1 /nobreak > nul & del /f /q \"%s\\Uninstall.exe\" & rmdir /s /q \"%s\"",
            installDir, installDir
        );
        ShellExecuteW(NULL, L"open", L"cmd.exe", cmd, NULL, SW_HIDE);
    }

    MessageBoxW(NULL, L"卸载完成。", L"提示", MB_ICONINFORMATION | MB_OK);
}

// Window Procedure for Installer Dialog
static LRESULT CALLBACK InstallerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hFontTitle = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
            g_hFontBody = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");

            // Title
            HWND hTitle = CreateWindowExW(0, L"STATIC", L"rapoo-tray Setup", WS_CHILD | WS_VISIBLE, 24, 20, 360, 24, hWnd, NULL, g_hInstance, NULL);
            SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

            // Install Path
            WCHAR installDir[MAX_PATH];
            GetInstallDir(installDir, MAX_PATH);
            WCHAR pathMsg[MAX_PATH + 32];
            StringCchPrintfW(pathMsg, MAX_PATH + 32, L"安装路径: %s", installDir);
            HWND hPath = CreateWindowExW(0, L"STATIC", pathMsg, WS_CHILD | WS_VISIBLE, 24, 56, 380, 20, hWnd, NULL, g_hInstance, NULL);
            SendMessageW(hPath, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);

            // Options
            g_hChkAutoRun = CreateWindowExW(0, L"BUTTON", L"开机自动启动", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 24, 90, 200, 22, hWnd, (HMENU)IDC_CHK_AUTORUN, g_hInstance, NULL);
            SendMessageW(g_hChkAutoRun, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);
            SendMessageW(g_hChkAutoRun, BM_SETCHECK, BST_CHECKED, 0);

            g_hChkDesktop = CreateWindowExW(0, L"BUTTON", L"创建桌面快捷方式", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 24, 118, 200, 22, hWnd, (HMENU)IDC_CHK_DESKTOP, g_hInstance, NULL);
            SendMessageW(g_hChkDesktop, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);
            SendMessageW(g_hChkDesktop, BM_SETCHECK, BST_CHECKED, 0);

            g_hChkLaunch = CreateWindowExW(0, L"BUTTON", L"安装完成后立即运行", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 24, 146, 200, 22, hWnd, (HMENU)IDC_CHK_LAUNCH, g_hInstance, NULL);
            SendMessageW(g_hChkLaunch, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);
            SendMessageW(g_hChkLaunch, BM_SETCHECK, BST_CHECKED, 0);

            // Buttons
            g_hBtnInstall = CreateWindowExW(0, L"BUTTON", L"安装", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 216, 186, 88, 30, hWnd, (HMENU)IDC_BTN_INSTALL, g_hInstance, NULL);
            SendMessageW(g_hBtnInstall, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);

            g_hBtnCancel = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 314, 186, 88, 30, hWnd, (HMENU)IDC_BTN_CANCEL, g_hInstance, NULL);
            SendMessageW(g_hBtnCancel, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);

            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetBkMode(hdcStatic, TRANSPARENT);
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == IDC_BTN_INSTALL) {
                DoInstall();
            } else if (wmId == IDC_BTN_CANCEL) {
                DestroyWindow(hWnd);
            }
            return 0;
        }

        case WM_DESTROY: {
            if (g_hFontTitle) DeleteObject(g_hFontTitle);
            if (g_hFontBody) DeleteObject(g_hFontBody);
            PostQuitMessage(0);
            return 0;
        }
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    g_hInstance = hInstance;
    CoInitialize(NULL);

    // Check if invoked as uninstaller
    WCHAR selfPath[MAX_PATH];
    GetModuleFileNameW(NULL, selfPath, MAX_PATH);

    bool isUninstall = false;
    if (wcsstr(pCmdLine, L"/uninstall") || wcsstr(pCmdLine, L"/UNINSTALL") || wcsstr(selfPath, L"Uninstall.exe") || wcsstr(selfPath, L"uninstall.exe")) {
        isUninstall = true;
    }

    if (isUninstall) {
        DoUninstall();
        CoUninitialize();
        return 0;
    }

    // Modern DPI awareness
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        typedef BOOL (WINAPI *pfnSetDpiAwareV2)(DPI_AWARENESS_CONTEXT);
        pfnSetDpiAwareV2 fnSetDpiAwareV2 = (pfnSetDpiAwareV2)GetProcAddress(hUser, "SetProcessDpiAwarenessContext");
        if (fnSetDpiAwareV2) {
            fnSetDpiAwareV2(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    // Register Window Class
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = InstallerWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"rapoo-traySetupWndClass";
    RegisterClassExW(&wc);

    int winWidth = 436;
    int winHeight = 265;
    int screenX = (GetSystemMetrics(SM_CXSCREEN) - winWidth) / 2;
    int screenY = (GetSystemMetrics(SM_CYSCREEN) - winHeight) / 2;

    g_hMainWnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        wc.lpszClassName,
        L"rapoo-tray Setup",
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        screenX, screenY, winWidth, winHeight,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hMainWnd) {
        CoUninitialize();
        return 1;
    }

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}
