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
#include "WindowEffects.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <QQuickWindow>
#include <QOperatingSystemVersion>
#include <windows.h>

enum {
    DWMWA_USE_IMMERSIVE_DARK_MODE = 20,
};

typedef HRESULT(WINAPI *DwmSetWindowAttributeFunc)(HWND, DWORD, LPCVOID, DWORD);

static void applyDarkTitleBar(HWND hwnd, bool dark)
{
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (!dwmapi) return;
    auto fn = reinterpret_cast<DwmSetWindowAttributeFunc>(
        GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
    if (!fn) {
        FreeLibrary(dwmapi);
        return;
    }

    BOOL useDark = dark ? TRUE : FALSE;
    fn(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));

    FreeLibrary(dwmapi);
}
#endif

namespace WindowEffects {

void applyTransparency(QQuickWindow *window, bool enabled, bool dark)
{
#ifdef Q_OS_WIN
    if (!window) return;
    if (QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11) {
        // The Mica backdrop (DWMWA_SYSTEMBACKDROP_TYPE) is disabled: on some
        // Windows 11 builds its region is stale after the window is realized
        // (a persistent gray block that does not follow resizes) and, combined
        // with the translucent window, it leaves the whole content area gray.
        // Only the dark-mode title bar is updated, which is safe on every
        // theme change.
        applyDarkTitleBar(reinterpret_cast<HWND>(window->winId()), dark);
    }
#else
    Q_UNUSED(window)
    Q_UNUSED(enabled)
    Q_UNUSED(dark)
#endif
}

}
