#include "WindowEffects.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <QQuickWindow>
#include <QOperatingSystemVersion>
#include <windows.h>

enum {
    DWMWA_USE_IMMERSIVE_DARK_MODE = 20,
    DWMWA_SYSTEMBACKDROP_TYPE = 38,
};

enum {
    DWMSBT_AUTO = 0,
    DWMSBT_MICA = 2,
};

typedef HRESULT(WINAPI *DwmSetWindowAttributeFunc)(HWND, DWORD, LPCVOID, DWORD);

static void applyMica(HWND hwnd, bool enabled, bool dark)
{
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (!dwmapi) return;
    auto fn = reinterpret_cast<DwmSetWindowAttributeFunc>(
        GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
    if (!fn) {
        FreeLibrary(dwmapi);
        return;
    }

    int backdrop = enabled ? DWMSBT_MICA : DWMSBT_AUTO;
    fn(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

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
    if (QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11)
        applyMica(reinterpret_cast<HWND>(window->winId()), enabled, dark);
#else
    Q_UNUSED(window)
    Q_UNUSED(enabled)
    Q_UNUSED(dark)
#endif
}

}
