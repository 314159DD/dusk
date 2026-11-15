#include <windows.h>
#include <shellapi.h>
#include "dusk.h"

static UINT g_taskbarMsg;
static NOTIFYICONDATAW g_nid;
static bool g_trayOn;

static void trayAdd()
{
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_main;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    int sm = GetSystemMetrics(SM_CXSMICON);
    g_nid.hIcon = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON, sm, sm, LR_DEFAULTCOLOR);
    lstrcpyW(g_nid.szTip, L"Dusk");
    g_trayOn = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
}

static void trayRemove()
{
    if (g_trayOn) Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_trayOn = false;
    if (g_nid.hIcon) DestroyIcon(g_nid.hIcon);
    g_nid.hIcon = NULL;
}

static void trayMenu()
{
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"Open");
    AppendMenuW(m, MF_STRING, 2, L"Quit");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_main);
    int cmd = TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, g_main, NULL);
    PostMessageW(g_main, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd == 1) uiShowPanel();
    else if (cmd == 2) PostMessageW(g_main, WM_CLOSE, 0, 0);
}

static void onHotkey(int id)
{
    if (id < 1 || id > g_set.nhk) return;
    const wchar_t* action = g_set.hk[id - 1].action;
    int cur = ctlMonitorUnderCursor();
    if (cur < 0) return;

    if (lstrcmpW(action, L"Toggle") == 0) {
        ctlSetEnabled(!g_set.enabled);
        uiFlash(cur, L"All screens", g_set.enabled ? L"On" : L"Paused");
        return;
    }

    bool all = lstrlenW(action) >= 3 && action[0] == 'A' && action[1] == 'l' && action[2] == 'l';
    const wchar_t* step = all ? action + 3 : action;
    int lo = all ? 0 : cur, hi = all ? g_nmon : cur + 1;
    for (int i = lo; i < hi; i++) {
        MonSetting* v = ctlGet(i);
        if (lstrcmpW(step, L"BrightnessUp") == 0) ctlSet(i, NOVAL, v->brightness + g_set.brightnessStep);
        else if (lstrcmpW(step, L"BrightnessDown") == 0) ctlSet(i, NOVAL, v->brightness - g_set.brightnessStep);
        else if (lstrcmpW(step, L"Cooler") == 0) ctlSet(i, v->kelvin + g_set.kelvinStep, NOVAL);
        else if (lstrcmpW(step, L"Warmer") == 0) ctlSet(i, v->kelvin - g_set.kelvinStep, NOVAL);
    }
    MonSetting* shown = ctlGet(cur);
    wchar_t text[48];
    wsprintfW(text, L"%d K   %d %%", shown->kelvin, shown->brightness);
    uiFlash(cur, all ? L"All screens" : g_mon[cur].name, text);
}

static LRESULT CALLBACK mainProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_HOTKEY:
        onHotkey((int)w);
        return 0;
    case WM_TIMER:
        if (w == TIMER_KEEP) ctlRampsApply();
        else if (w == TIMER_SAVE) ctlFlush();
        return 0;
    case WM_DISPLAYCHANGE:
        monitorsRefresh();
        ctlApplyAll();
        uiRebuild();
        return 0;
    case WM_POWERBROADCAST:
        if (w == PBT_APMRESUMEAUTOMATIC || w == PBT_APMRESUMESUSPEND) ctlApplyAll();
        return TRUE;
    case WM_TRAY:
        if (LOWORD(l) == WM_LBUTTONUP) uiTogglePanel();
        else if (LOWORD(l) == WM_RBUTTONUP) trayMenu();
        return 0;
    case WM_ENDSESSION:
        if (w) ctlFlush();
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        KillTimer(h, TIMER_KEEP);
        hotkeysUnregister(h);
        uiShutdown();
        ctlFlush();
        ctlRestoreAll();
        trayRemove();
        PostQuitMessage(0);
        return 0;
    }
    if (m == g_taskbarMsg && m != 0) {
        g_trayOn = false;
        trayAdd();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static bool makeDirs(const wchar_t* path)
{
    wchar_t t[MAX_PATH];
    lstrcpynW(t, path, MAX_PATH);
    for (wchar_t* p = t + 1; *p; p++) {
        if (*p == '\\' || *p == '/') {
            wchar_t c = *p;
            *p = 0;
            CreateDirectoryW(t, NULL);
            *p = c;
        }
    }
    CreateDirectoryW(t, NULL);
    DWORD a = GetFileAttributesW(t);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int selfTest(const wchar_t* folder)
{
    if (!makeDirs(folder)) return 2;
    settingsLoad();
    monitorsRefresh();

    char* out = (char*)HeapAlloc(GetProcessHeap(), 0, 64 * 1024);
    if (!out) return 3;
    int n = 0;
    for (int i = 0; i < g_nmon; i++) {
        unsigned short ramp[768];
        HDC dc = CreateDCW(NULL, g_mon[i].device, NULL, NULL);
        bool read = dc && GetDeviceGammaRamp(dc, ramp);
        bool write = read && SetDeviceGammaRamp(dc, ramp);
        if (dc) DeleteDC(dc);
        wchar_t line[700];
        wsprintfW(line, L"%s | %s | %dx%d | read=%s write=%s | id=%s\r\n", g_mon[i].name, g_mon[i].device,
            g_mon[i].bounds.right - g_mon[i].bounds.left, g_mon[i].bounds.bottom - g_mon[i].bounds.top,
            read ? L"True" : L"False", write ? L"True" : L"False", g_mon[i].id);
        n += WideCharToMultiByte(CP_UTF8, 0, line, -1, out + n, 64 * 1024 - n - 1, NULL, NULL) - 1;
    }
    wchar_t path[MAX_PATH + 16];
    wsprintfW(path, L"%s\\probe.txt", folder);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD wrote;
        WriteFile(f, out, n, &wrote, NULL);
        CloseHandle(f);
    }
    HeapFree(GetProcessHeap(), 0, out);

    wsprintfW(path, L"%s\\panel.png", folder);
    return uiSelfTest(path) ? 0 : 4;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    g_inst = inst;

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool tray = false;
    if (argv) {
        if (argc >= 3 && lstrcmpiW(argv[1], L"/selftest") == 0) {
            int rc = selfTest(argv[2]);
            LocalFree(argv);
            return rc;
        }
        for (int i = 1; i < argc; i++)
            if (lstrcmpiW(argv[i], L"/tray") == 0) tray = true;
        LocalFree(argv);
    }

    HANDLE mutex = CreateMutexW(NULL, TRUE, L"Dusk.SingleInstance");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    settingsLoad();
    monitorsRefresh();
    ctlApplyAll();

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.hInstance = inst;
    wc.lpszClassName = L"DuskMain";
    wc.lpfnWndProc = mainProc;
    RegisterClassW(&wc);
    g_main = CreateWindowExW(0, L"DuskMain", L"Dusk", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    if (!g_main) return 1;

    hotkeysRegister(g_main);
    g_taskbarMsg = RegisterWindowMessageW(L"TaskbarCreated");
    trayAdd();
    SetTimer(g_main, TIMER_KEEP, 3000, NULL);
    if (!tray) uiShowPanel();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
