#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BEACON_DIR "beacon"
#define BEACON_EXE "Beacon.exe"
#define IDR_BEACON_ZIP 101

static void log_msg(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    char path[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, path);
    strcat(path, "\\beacon_install.log");

    HANDLE hFile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        SetFilePointer(hFile, 0, NULL, FILE_END);
        char line[1152];
        SYSTEMTIME st;
        GetLocalTime(&st);
        int len = snprintf(line, sizeof(line), "[%02d:%02d:%02d] %s\n",
                          st.wHour, st.wMinute, st.wSecond, buf);
        DWORD written;
        WriteFile(hFile, line, len, &written, NULL);
        CloseHandle(hFile);
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nShow)
{
    (void)hInst; (void)hPrev; (void)nShow;

    log_msg("BeaconLauncher started");

    char cwd[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, cwd);
    log_msg("CWD: %s", cwd);

    char beacon_dir[MAX_PATH];
    snprintf(beacon_dir, sizeof(beacon_dir), "%s\\%s", cwd, BEACON_DIR);
    log_msg("Beacon dir: %s", beacon_dir);

    char beacon_exe[MAX_PATH];
    snprintf(beacon_exe, sizeof(beacon_exe), "%s\\%s", beacon_dir, BEACON_EXE);
    log_msg("Beacon exe path: %s", beacon_exe);

    DWORD attr = GetFileAttributesA(beacon_dir);
    int needs_extract = (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY));
    log_msg("Needs extract: %d (attr=0x%08lX)", needs_extract, attr);

    if (needs_extract) {
        /* Load embedded ZIP from resources */
        HRSRC hRes = FindResourceA(hInst, MAKEINTRESOURCE(IDR_BEACON_ZIP), RT_RCDATA);
        if (!hRes) {
            log_msg("ERROR: FindResource failed, error=%lu", GetLastError());
            MessageBoxA(NULL, "提取失败：无法找到嵌入数据。\n启动器可能已损坏。",
                       "Beacon Launcher", MB_ICONERROR);
            return 1;
        }

        DWORD zip_size = SizeofResource(hInst, hRes);
        HGLOBAL hMem = LoadResource(hInst, hRes);
        if (!hMem || !zip_size) {
            log_msg("ERROR: LoadResource failed");
            MessageBoxA(NULL, "提取失败：无法加载嵌入数据。",
                       "Beacon Launcher", MB_ICONERROR);
            return 1;
        }

        void *zip_data = LockResource(hMem);
        log_msg("Embedded ZIP: %lu bytes", zip_size);

        /* Show centered extraction dialog */
        int dlgW = 300, dlgH = 80;
        int scrW = GetSystemMetrics(SM_CXSCREEN);
        int scrH = GetSystemMetrics(SM_CYSCREEN);
        HWND hWnd = CreateWindowExA(0, "STATIC", "Beacon Launcher",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
            (scrW - dlgW) / 2, (scrH - dlgH) / 2, dlgW, dlgH,
            NULL, NULL, hInst, NULL);
        if (hWnd) {
            HFONT hFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
            if (hFont) SendMessageA(hWnd, WM_SETFONT, (WPARAM)hFont, TRUE);
            CreateWindowExA(0, "STATIC",
                "Updating...",
                WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_CENTER,
                0, 0, dlgW, dlgH,
                hWnd, NULL, hInst, NULL);
            ShowWindow(hWnd, SW_SHOW);
            UpdateWindow(hWnd);
        }

        int ok = 0;

        /* Write ZIP data to temporary file (.zip extension required by Expand-Archive) */
        char temp_zip[MAX_PATH];
        {
            char temp_raw[MAX_PATH];
            char temp_dir[MAX_PATH];
            GetTempPathA(sizeof(temp_dir), temp_dir);
            GetTempFileNameA(temp_dir, "BZ", 0, temp_raw);
            DeleteFileA(temp_raw);
            size_t len = strlen(temp_raw);
            if (len > 4 && temp_raw[len-4] == '.')
                memcpy(temp_raw + len - 4, ".zip", 4);
            else
                strcat(temp_raw, ".zip");
            lstrcpyA(temp_zip, temp_raw);
        }
        log_msg("Temp ZIP: %s", temp_zip);

        {
            HANDLE hTemp = CreateFileA(temp_zip, GENERIC_WRITE, 0, NULL,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hTemp != INVALID_HANDLE_VALUE) {
                DWORD written;
                ok = WriteFile(hTemp, zip_data, zip_size, &written, NULL)
                     && written == zip_size;
                CloseHandle(hTemp);
            }
        }

        if (ok) {
            char cmd[4096];
            snprintf(cmd, sizeof(cmd),
                "powershell -NoProfile -Command \"& { Add-Type -Assembly System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::ExtractToDirectory('%s', '%s') }\"",
                temp_zip, beacon_dir);
            log_msg("Running: %s", cmd);

            STARTUPINFOA si = {0};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi = {0};
            if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                               NULL, NULL, &si, &pi)) {
                for (;;) {
                    DWORD rc = MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, 100, QS_ALLINPUT);
                    if (rc == WAIT_OBJECT_0) break;
                    MSG msg;
                    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&msg);
                        DispatchMessageA(&msg);
                    }
                }
                DWORD exit_code;
                ok = (GetExitCodeProcess(pi.hProcess, &exit_code) && exit_code == 0);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                log_msg("PowerShell exit code: %lu", exit_code);
            } else {
                log_msg("ERROR: CreateProcess failed, error=%lu", GetLastError());
                ok = 0;
            }

            DeleteFileA(temp_zip);
        }

        if (hWnd) DestroyWindow(hWnd);

        if (!ok) {
            log_msg("ERROR: Extraction failed");
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "cmd.exe /c rmdir /s /q \"%s\"", beacon_dir);
            WinExec(cmd, SW_HIDE);
            MessageBoxA(NULL, "提取失败，请检查磁盘空间后重试。",
                       "Beacon Launcher", MB_ICONERROR);
            return 1;
        }
        log_msg("Extraction complete");
    }

    /* Verify Beacon.exe exists */
    attr = GetFileAttributesA(beacon_exe);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        log_msg("ERROR: %s not found", beacon_exe);
        MessageBoxA(NULL, "Beacon.exe 未找到，安装可能不完整。",
                   "Beacon Launcher", MB_ICONERROR);
        return 1;
    }

    log_msg("Launching: %s", beacon_exe);

    SHELLEXECUTEINFOA sei;
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.lpVerb = "open";
    sei.lpFile = beacon_exe;
    sei.lpDirectory = beacon_dir;
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;

    if (!ShellExecuteExA(&sei)) {
        log_msg("ERROR: ShellExecuteExA failed, error=%lu", GetLastError());
        MessageBoxA(NULL, "启动 Beacon.exe 失败，请尝试以管理员身份运行。",
                   "Beacon Launcher", MB_ICONERROR);
        return 1;
    }

    log_msg("Beacon launched successfully");
    return 0;
}
