/*
 * Beacon - a cross-platform Minecraft launcher.
 *
 * Copyright (C) 2024-2026 fuqicn
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
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
#define IDI_BEACON_ICON 102
#define WINDOW_TITLE "Beacon"
#define VERSION_FILE "version.txt"

/* Bump this every release; it must match the version.txt written by the
   packaging script so an existing install knows it needs re-extracting. */
#define BEACON_VERSION "1.0.1"

/* User data that must survive an update: never delete or overwrite these. */
static const char *kPreserveDirs[] = {
    ".minecraft", "auth", "cache", "instances", "generic"
};
static const char *kPreserveFiles[] = {
    "settings.ini"
};

/* ---- Locale-aware strings ---- */
static int g_zh = 1;
static const char *L_READY;
static const char *L_UPDATING;
static const char *L_INIT;
static const char *L_ERR_RESOURCE;
static const char *L_ERR_LOAD;
static const char *L_ERR_EXTRACT;
static const char *L_ERR_NOTFOUND;
static const char *L_ERR_LAUNCH;
static const char *L_TITLE;

static void init_lang(void) {
    g_zh = (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE);
    if (g_zh) {
        L_TITLE         = "Beacon Launcher";
        L_READY         = "正在准备...";
        L_UPDATING      = "正在更新 Beacon，请稍候...";
        L_INIT          = "正在初始化 Beacon，请稍候...";
        L_ERR_RESOURCE  = "提取失败：无法找到嵌入数据。\n启动器可能已损坏。";
        L_ERR_LOAD      = "提取失败：无法加载嵌入数据。";
        L_ERR_EXTRACT   = "提取失败，请检查磁盘空间后重试。";
        L_ERR_NOTFOUND  = "Beacon.exe 未找到，安装可能不完整。";
        L_ERR_LAUNCH    = "启动 Beacon.exe 失败，请尝试以管理员身份运行。";
    } else {
        L_TITLE         = "Beacon Launcher";
        L_READY         = "Preparing...";
        L_UPDATING      = "Updating Beacon, please wait...";
        L_INIT          = "Initializing Beacon, please wait...";
        L_ERR_RESOURCE  = "Extraction failed: embedded data not found.\nThe launcher may be corrupted.";
        L_ERR_LOAD      = "Extraction failed: could not load embedded data.";
        L_ERR_EXTRACT   = "Extraction failed. Check disk space and try again.";
        L_ERR_NOTFOUND  = "Beacon.exe not found. The installation may be incomplete.";
        L_ERR_LAUNCH    = "Failed to start Beacon.exe. Try running as administrator.";
    }
}

static HWND g_hWnd = NULL;
static HWND g_hProgress = NULL;
static HWND g_hLabel = NULL;
static int g_progress_pos = 0;

/* Phase drives a single 0..100 sweep (extract caps at 90, launch 90->98,
   window-found jumps to 100). It never resets, so it never loops. */
enum { PHASE_EXTRACT = 0, PHASE_LAUNCH = 1, PHASE_DONE = 2 };
static int g_phase = PHASE_EXTRACT;

static void CALLBACK progress_tick(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    (void)hwnd; (void)uMsg; (void)idEvent; (void)dwTime;
    if (!g_hProgress) return;
    if (g_phase == PHASE_EXTRACT) {
        if (g_progress_pos < 90) g_progress_pos += 1;
    } else if (g_phase == PHASE_LAUNCH) {
        if (g_progress_pos < 98) g_progress_pos += 1;
    }
    SendMessageA(g_hProgress, PBM_SETPOS, (WPARAM)g_progress_pos, 0);
}

static void set_progress(int pos) {
    g_progress_pos = pos;
    if (g_hProgress) SendMessageA(g_hProgress, PBM_SETPOS, (WPARAM)pos, 0);
}

/* Source is UTF-8; Windows GUI text APIs need UTF-16. */
static wchar_t *utf8_to_wide(const char *utf8) {
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, n);
    return w;
}

static void set_label(const char *text) {
    if (g_hLabel) {
        wchar_t *w = utf8_to_wide(text);
        SetWindowTextW(g_hLabel, w);
        free(w);
        UpdateWindow(g_hLabel);
    }
}

static void error_box(const char *text) {
    wchar_t *wt = utf8_to_wide(text);
    wchar_t *wc = utf8_to_wide(L_TITLE);
    MessageBoxW(NULL, wt, wc, MB_ICONERROR);
    free(wt);
    free(wc);
}

static void pump_messages(void) {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

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

static HWND create_dialog(HINSTANCE hInst) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    int dlgW = 380, dlgH = 120;
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);

    HWND hWnd = CreateWindowExA(0, "STATIC", L_TITLE,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        (scrW - dlgW) / 2, (scrH - dlgH) / 2, dlgW, dlgH,
        NULL, NULL, hInst, NULL);
    if (!hWnd) return NULL;

    HICON hIcon = LoadIconA(hInst, MAKEINTRESOURCE(IDI_BEACON_ICON));
    if (hIcon) {
        SendMessageA(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageA(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    }

    HFONT hFont = CreateFontA(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, "Microsoft YaHei");
    if (hFont) SendMessageA(hWnd, WM_SETFONT, (WPARAM)hFont, TRUE);

    wchar_t *wReady = utf8_to_wide(L_READY);
    g_hLabel = CreateWindowExW(0, L"STATIC", wReady,
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        12, 16, dlgW - 24, 28,
        hWnd, NULL, hInst, NULL);
    free(wReady);
    if (hFont && g_hLabel) SendMessageA(g_hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hProgress = CreateWindowExA(0, PROGRESS_CLASS, "",
        WS_CHILD | WS_VISIBLE,
        12, 60, dlgW - 24, 18,
        hWnd, NULL, hInst, NULL);
    if (g_hProgress) {
        SendMessageA(g_hProgress, PBM_SETRANGE32, 0, 100);
        SendMessageA(g_hProgress, PBM_SETPOS, 0, 0);
        SetTimer(hWnd, 1, 30, progress_tick);
    }

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);
    return hWnd;
}

static void wait_for_beacon_window(void) {
    int timeout_ms = 120000;
    int waited = 0;
    while (waited < timeout_ms) {
        HWND hFound = FindWindowA(NULL, WINDOW_TITLE);
        if (hFound) {
            log_msg("Beacon window found, closing progress dialog");
            return;
        }
        pump_messages();
        Sleep(50);
        waited += 50;
    }
    log_msg("Timeout waiting for Beacon window");
}

static int file_exists(const char *path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES;
}

/* Read first line of a UTF-8 text file (e.g. version.txt). Returns 1 on
   success, 0 if the file is missing. */
static int read_text_file(const char *path, char *buf, int cap) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(buf, cap, f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) {
        buf[n - 1] = '\0';
        n--;
    }
    return 1;
}

/* Recursively delete a directory tree. */
static void delete_tree(const char *path) {
    char search[MAX_PATH];
    snprintf(search, sizeof(search), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
                continue;
            char child[MAX_PATH];
            snprintf(child, sizeof(child), "%s\\%s", path, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                delete_tree(child);
            else
                DeleteFileA(child);
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    RemoveDirectoryA(path);
}

/* Move user-data dirs/files from old_dir into new_dir so an update never
   touches them. Returns 1 if all preserved items were handled (missing ones
   are fine). */
static int preserve_user_data(const char *old_dir, const char *new_dir) {
    char src[MAX_PATH], dst[MAX_PATH];
    int i;

    for (i = 0; i < (int)(sizeof(kPreserveDirs) / sizeof(kPreserveDirs[0])); i++) {
        snprintf(src, sizeof(src), "%s\\%s", old_dir, kPreserveDirs[i]);
        snprintf(dst, sizeof(dst), "%s\\%s", new_dir, kPreserveDirs[i]);
        if (!file_exists(src)) continue;
        log_msg("Preserving dir: %s", src);
        if (file_exists(dst)) delete_tree(dst);
        if (!MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING)) {
            log_msg("ERROR: failed to move preserved dir %s, err=%lu",
                    src, GetLastError());
            return 0;
        }
    }

    for (i = 0; i < (int)(sizeof(kPreserveFiles) / sizeof(kPreserveFiles[0])); i++) {
        snprintf(src, sizeof(src), "%s\\%s", old_dir, kPreserveFiles[i]);
        snprintf(dst, sizeof(dst), "%s\\%s", new_dir, kPreserveFiles[i]);
        if (!file_exists(src)) continue;
        log_msg("Preserving file: %s", src);
        if (file_exists(dst)) DeleteFileA(dst);
        if (!MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING)) {
            log_msg("ERROR: failed to move preserved file %s, err=%lu",
                    src, GetLastError());
            return 0;
        }
    }
    return 1;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nShow)
{
    (void)hInst; (void)hPrev; (void)nShow;

    init_lang();
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

    /* Decide whether the embedded payload must be (re)extracted: the version
       file must exist and match the hardcoded version. */
    char ver_path[MAX_PATH];
    snprintf(ver_path, sizeof(ver_path), "%s\\%s", beacon_dir, VERSION_FILE);
    char cur_version[64] = "";
    int has_version = read_text_file(ver_path, cur_version, sizeof(cur_version));
    int needs_extract = !has_version || strcmp(cur_version, BEACON_VERSION) != 0;
    log_msg("Installed version: '%s', expected: '%s', needs extract: %d",
            has_version ? cur_version : "(none)", BEACON_VERSION, needs_extract);

    if (needs_extract) {
        /* Load embedded ZIP from resources */
        HRSRC hRes = FindResourceA(hInst, MAKEINTRESOURCE(IDR_BEACON_ZIP), RT_RCDATA);
        if (!hRes) {
            log_msg("ERROR: FindResource failed, error=%lu", GetLastError());
            error_box(L_ERR_RESOURCE);
            return 1;
        }

        DWORD zip_size = SizeofResource(hInst, hRes);
        HGLOBAL hMem = LoadResource(hInst, hRes);
        if (!hMem || !zip_size) {
            log_msg("ERROR: LoadResource failed");
            error_box(L_ERR_LOAD);
            return 1;
        }

        void *zip_data = LockResource(hMem);
        log_msg("Embedded ZIP: %lu bytes", zip_size);

        g_hWnd = create_dialog(hInst);
        set_label(L_UPDATING);
        g_phase = PHASE_EXTRACT;
        set_progress(0);

        int ok = 0;

        /* Write ZIP data to temporary file (.zip extension required) */
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

        /* Extract into a fresh temp dir so user data in the old install can
           be moved over before the old dir is swapped out. */
        char new_dir[MAX_PATH];
        snprintf(new_dir, sizeof(new_dir), "%s.tmp", beacon_dir);
        if (ok) {
            if (file_exists(new_dir)) delete_tree(new_dir);
            CreateDirectoryA(new_dir, NULL);

            char cmd[4096];
            snprintf(cmd, sizeof(cmd),
                "powershell -NoProfile -Command \"& { Add-Type -Assembly System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::ExtractToDirectory('%s', '%s') }\"",
                temp_zip, new_dir);
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
        }

        if (ok) {
            /* Move user data from the old install into the fresh dir, then
               swap directories. */
            if (file_exists(beacon_dir)) {
                ok = preserve_user_data(beacon_dir, new_dir);
                if (ok) {
                    log_msg("Removing old install: %s", beacon_dir);
                    delete_tree(beacon_dir);
                }
            }
            if (ok) {
                if (!MoveFileExA(new_dir, beacon_dir, MOVEFILE_REPLACE_EXISTING)) {
                    log_msg("ERROR: failed to swap dirs, err=%lu", GetLastError());
                    ok = 0;
                }
            }
        }

        DeleteFileA(temp_zip);

        if (!ok) {
            if (file_exists(beacon_dir)) delete_tree(beacon_dir);
            if (file_exists(new_dir)) delete_tree(new_dir);
            if (g_hWnd) DestroyWindow(g_hWnd);
            log_msg("ERROR: Extraction failed");
            error_box(L_ERR_EXTRACT);
            return 1;
        }
        log_msg("Extraction complete");
    } else {
        /* Already up to date: show the progress dialog during startup */
        g_hWnd = create_dialog(hInst);
        set_label(L_INIT);
        g_phase = PHASE_LAUNCH;
        set_progress(90);
    }

    /* Verify Beacon.exe exists */
    DWORD attr = GetFileAttributesA(beacon_exe);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        if (g_hWnd) DestroyWindow(g_hWnd);
        log_msg("ERROR: %s not found", beacon_exe);
        error_box(L_ERR_NOTFOUND);
        return 1;
    }

    log_msg("Launching: %s", beacon_exe);

    set_label(L_INIT);
    g_phase = PHASE_LAUNCH;

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessA(NULL, (LPSTR)beacon_exe, NULL, NULL, FALSE,
                        CREATE_UNICODE_ENVIRONMENT, NULL, beacon_dir, &si, &pi)) {
        if (g_hWnd) DestroyWindow(g_hWnd);
        log_msg("ERROR: CreateProcess failed for %s, error=%lu", beacon_exe, GetLastError());
        error_box(L_ERR_LAUNCH);
        return 1;
    }
    CloseHandle(pi.hThread);

    log_msg("Beacon launched, waiting for main window");
    wait_for_beacon_window();

    CloseHandle(pi.hProcess);

    if (g_hWnd) {
        g_phase = PHASE_DONE;
        set_progress(100);
        Sleep(250);
        DestroyWindow(g_hWnd);
    }
    log_msg("Beacon launched successfully");
    return 0;
}
