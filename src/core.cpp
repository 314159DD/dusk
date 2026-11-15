#include <windows.h>
#include <math.h>
#include <wchar.h>
#include "dusk.h"

Settings g_set;
Monitor g_mon[MAXMON];
int g_nmon;
HWND g_main;
HINSTANCE g_inst;
wchar_t g_failed[MAXHK][64];
int g_nfailed;

static const Preset kDefaultPresets[3] = { { 6500, 100 }, { 4200, 85 }, { 2700, 60 } };

static const wchar_t* const kDefaultKeys[][2] = {
    { L"BrightnessUp", L"Alt+PageUp" },
    { L"BrightnessDown", L"Alt+PageDown" },
    { L"Cooler", L"Alt+Shift+PageUp" },
    { L"Warmer", L"Alt+Shift+PageDown" },
    { L"AllBrightnessUp", L"Ctrl+Alt+PageUp" },
    { L"AllBrightnessDown", L"Ctrl+Alt+PageDown" },
    { L"AllCooler", L"Ctrl+Alt+Shift+PageUp" },
    { L"AllWarmer", L"Ctrl+Alt+Shift+PageDown" },
    { L"Toggle", L"Ctrl+Alt+Home" },
};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void copyStr(wchar_t* dst, int cap, const wchar_t* src)
{
    lstrcpynW(dst, src, cap);
}

static void monDefaults(MonSetting* m)
{
    ZeroMemory(m, sizeof(*m));
    m->kelvin = 6500;
    m->brightness = 100;
    m->filterKelvin = 3400;
    m->filterReturn = 6500;
}

static void monNormalize(MonSetting* m)
{
    while (m->np < 3) {
        m->presets[m->np] = kDefaultPresets[m->np];
        m->np++;
    }
}

static void settingsDefaults()
{
    ZeroMemory(&g_set, sizeof(g_set));
    g_set.enabled = true;
    g_set.kelvinStep = 200;
    g_set.brightnessStep = 5;
}

static void addDefaultHotkeys()
{
    for (int i = 0; i < 9; i++) {
        bool found = false;
        for (int j = 0; j < g_set.nhk; j++)
            if (lstrcmpW(g_set.hk[j].action, kDefaultKeys[i][0]) == 0) found = true;
        if (found || g_set.nhk >= MAXHK) continue;
        copyStr(g_set.hk[g_set.nhk].action, 32, kDefaultKeys[i][0]);
        copyStr(g_set.hk[g_set.nhk].keys, 64, kDefaultKeys[i][1]);
        g_set.nhk++;
    }
}

struct JP { const wchar_t* p; bool ok; };

static void ws(JP& j)
{
    while (*j.p == ' ' || *j.p == '\t' || *j.p == '\r' || *j.p == '\n') j.p++;
}

static bool lit(JP& j, wchar_t c)
{
    ws(j);
    if (*j.p == c) { j.p++; return true; }
    return false;
}

static bool jstr(JP& j, wchar_t* out, int cap)
{
    ws(j);
    if (*j.p != '"') { j.ok = false; return false; }
    j.p++;
    int n = 0;
    while (*j.p && *j.p != '"') {
        wchar_t c = *j.p++;
        if (c == '\\') {
            c = *j.p++;
            switch (c) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': {
                unsigned v = 0;
                for (int i = 0; i < 4; i++) {
                    wchar_t h = *j.p;
                    int d = (h >= '0' && h <= '9') ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
                    if (d < 0) { j.ok = false; return false; }
                    v = v * 16 + d;
                    j.p++;
                }
                c = (wchar_t)v;
                break;
            }
            case 0: j.ok = false; return false;
            default: break;
            }
        }
        if (out && n < cap - 1) out[n++] = c;
    }
    if (*j.p != '"') { j.ok = false; return false; }
    j.p++;
    if (out) out[n] = 0;
    return true;
}

static bool jnum(JP& j, int* out)
{
    ws(j);
    const wchar_t* s = j.p;
    bool neg = false;
    if (*j.p == '-') { neg = true; j.p++; }
    if (*j.p < '0' || *j.p > '9') { j.p = s; j.ok = false; return false; }
    double v = 0;
    while (*j.p >= '0' && *j.p <= '9') { v = v * 10 + (*j.p - '0'); j.p++; }
    if (*j.p == '.') {
        j.p++;
        while (*j.p >= '0' && *j.p <= '9') j.p++;
    }
    if (*j.p == 'e' || *j.p == 'E') {
        j.p++;
        if (*j.p == '+' || *j.p == '-') j.p++;
        while (*j.p >= '0' && *j.p <= '9') j.p++;
    }
    if (neg) v = -v;
    if (v > 2000000000.0) v = 2000000000.0;
    if (v < -2000000000.0) v = -2000000000.0;
    *out = (int)v;
    return true;
}

static void jskip(JP& j)
{
    ws(j);
    wchar_t c = *j.p;
    if (c == '"') { jstr(j, NULL, 0); return; }
    if (c == '{' || c == '[') {
        wchar_t close = c == '{' ? '}' : ']';
        j.p++;
        ws(j);
        if (*j.p == close) { j.p++; return; }
        for (;;) {
            if (c == '{') {
                if (!jstr(j, NULL, 0)) return;
                if (!lit(j, ':')) { j.ok = false; return; }
            }
            jskip(j);
            if (!j.ok) return;
            ws(j);
            if (*j.p == ',') { j.p++; continue; }
            if (*j.p == close) { j.p++; return; }
            j.ok = false;
            return;
        }
    }
    if (!c) { j.ok = false; return; }
    while (*j.p && *j.p != ',' && *j.p != '}' && *j.p != ']' && *j.p > ' ') j.p++;
}

static void getInt(JP& j, int* dst)
{
    ws(j);
    if (*j.p == 'n') { jskip(j); return; }
    int v;
    if (jnum(j, &v)) *dst = v;
}

static void getBool(JP& j, bool* dst)
{
    ws(j);
    if (j.p[0] == 't') { *dst = true; jskip(j); }
    else if (j.p[0] == 'f') { *dst = false; jskip(j); }
    else jskip(j);
}

static void getStr(JP& j, wchar_t* dst, int cap)
{
    ws(j);
    if (*j.p == 'n') { jskip(j); return; }
    jstr(j, dst, cap);
}

typedef void (*FieldFn)(JP&, const wchar_t*, void*);

static void parseObject(JP& j, FieldFn field, void* ctx)
{
    ws(j);
    if (*j.p == 'n') { jskip(j); return; }
    if (!lit(j, '{')) { j.ok = false; return; }
    ws(j);
    if (*j.p == '}') { j.p++; return; }
    for (;;) {
        wchar_t key[200];
        if (!jstr(j, key, 200)) return;
        if (!lit(j, ':')) { j.ok = false; return; }
        field(j, key, ctx);
        if (!j.ok) return;
        ws(j);
        if (*j.p == ',') { j.p++; continue; }
        if (*j.p == '}') { j.p++; return; }
        j.ok = false;
        return;
    }
}

static void presetField(JP& j, const wchar_t* key, void* ctx)
{
    Preset* p = (Preset*)ctx;
    if (lstrcmpW(key, L"Kelvin") == 0) getInt(j, &p->kelvin);
    else if (lstrcmpW(key, L"Brightness") == 0) getInt(j, &p->brightness);
    else jskip(j);
}

static void monField(JP& j, const wchar_t* key, void* ctx)
{
    MonSetting* m = (MonSetting*)ctx;
    if (lstrcmpW(key, L"Kelvin") == 0) getInt(j, &m->kelvin);
    else if (lstrcmpW(key, L"Brightness") == 0) getInt(j, &m->brightness);
    else if (lstrcmpW(key, L"FilterKelvin") == 0) getInt(j, &m->filterKelvin);
    else if (lstrcmpW(key, L"FilterReturnKelvin") == 0) getInt(j, &m->filterReturn);
    else if (lstrcmpW(key, L"Note") == 0) getStr(j, m->note, 128);
    else if (lstrcmpW(key, L"Presets") == 0) {
        ws(j);
        if (*j.p != '[') { jskip(j); return; }
        j.p++;
        ws(j);
        if (*j.p == ']') { j.p++; return; }
        for (;;) {
            Preset p = { 6500, 100 };
            parseObject(j, presetField, &p);
            if (!j.ok) return;
            if (m->np < 8) m->presets[m->np++] = p;
            ws(j);
            if (*j.p == ',') { j.p++; continue; }
            if (*j.p == ']') { j.p++; return; }
            j.ok = false;
            return;
        }
    }
    else jskip(j);
}

static void monitorsField(JP& j, const wchar_t* key, void*)
{
    MonSetting m;
    monDefaults(&m);
    copyStr(m.id, 200, key);
    parseObject(j, monField, &m);
    if (!j.ok) return;
    monNormalize(&m);
    if (g_set.nmon < MAXSET) g_set.mon[g_set.nmon++] = m;
}

static void hotkeysField(JP& j, const wchar_t* key, void*)
{
    wchar_t v[64];
    v[0] = 0;
    ws(j);
    if (*j.p == 'n') { jskip(j); return; }
    if (*j.p != '"') { jskip(j); return; }
    getStr(j, v, 64);
    if (!j.ok) return;
    for (int i = 0; i < g_set.nhk; i++)
        if (lstrcmpW(g_set.hk[i].action, key) == 0) { copyStr(g_set.hk[i].keys, 64, v); return; }
    if (g_set.nhk < MAXHK) {
        copyStr(g_set.hk[g_set.nhk].action, 32, key);
        copyStr(g_set.hk[g_set.nhk].keys, 64, v);
        g_set.nhk++;
    }
}

static void rootField(JP& j, const wchar_t* key, void*)
{
    if (lstrcmpW(key, L"Enabled") == 0) getBool(j, &g_set.enabled);
    else if (lstrcmpW(key, L"KelvinStep") == 0) getInt(j, &g_set.kelvinStep);
    else if (lstrcmpW(key, L"BrightnessStep") == 0) getInt(j, &g_set.brightnessStep);
    else if (lstrcmpW(key, L"Monitors") == 0) parseObject(j, monitorsField, NULL);
    else if (lstrcmpW(key, L"Hotkeys") == 0) parseObject(j, hotkeysField, NULL);
    else jskip(j);
}

static bool settingsPath(wchar_t* out, int cap, bool dirOnly)
{
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    wchar_t dir[MAX_PATH + 8];
    wsprintfW(dir, L"%s\\Dusk", base);
    if (dirOnly) { copyStr(out, cap, dir); return true; }
    wsprintfW(out, L"%s\\settings.json", dir);
    return true;
}

void settingsLoad()
{
    settingsDefaults();
    wchar_t path[MAX_PATH + 32];
    if (settingsPath(path, MAX_PATH + 32, false)) {
        HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD size = GetFileSize(f, NULL);
            if (size != INVALID_FILE_SIZE && size > 0 && size < 4u * 1024 * 1024) {
                BYTE* raw = (BYTE*)HeapAlloc(GetProcessHeap(), 0, size);
                DWORD got = 0;
                if (raw && ReadFile(f, raw, size, &got, NULL)) {
                    BYTE* src = raw;
                    int len = (int)got;
                    if (len >= 3 && src[0] == 0xEF && src[1] == 0xBB && src[2] == 0xBF) { src += 3; len -= 3; }
                    int wn = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)src, len, NULL, 0);
                    wchar_t* w = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (wn + 1) * sizeof(wchar_t));
                    if (w) {
                        MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)src, len, w, wn);
                        w[wn] = 0;
                        JP j = { w, true };
                        parseObject(j, rootField, NULL);
                        if (!j.ok) settingsDefaults();
                        HeapFree(GetProcessHeap(), 0, w);
                    }
                }
                if (raw) HeapFree(GetProcessHeap(), 0, raw);
            }
            CloseHandle(f);
        }
    }
    for (int i = 0; i < g_set.nmon; i++) monNormalize(&g_set.mon[i]);
    addDefaultHotkeys();
}

struct WBuf { char* b; int n; int cap; };

static void wput(WBuf& w, const char* s)
{
    int l = lstrlenA(s);
    if (w.n + l >= w.cap) return;
    CopyMemory(w.b + w.n, s, l);
    w.n += l;
}

static void wch(WBuf& w, char c)
{
    if (w.n + 1 >= w.cap) return;
    w.b[w.n++] = c;
}

static void wint(WBuf& w, int v)
{
    char t[16];
    wsprintfA(t, "%d", v);
    wput(w, t);
}

static void windent(WBuf& w, int n)
{
    for (int i = 0; i < n; i++) wput(w, "  ");
}

static void wstr(WBuf& w, const wchar_t* s)
{
    wch(w, '"');
    for (; *s; s++) {
        unsigned c = *s;
        if (c == '"') wput(w, "\\\"");
        else if (c == '\\') wput(w, "\\\\");
        else if (c == '\n') wput(w, "\\n");
        else if (c == '\r') wput(w, "\\r");
        else if (c == '\t') wput(w, "\\t");
        else if (c < 0x20) {
            char t[8];
            wsprintfA(t, "\\u%04X", c);
            wput(w, t);
        }
        else if (c < 0x80) wch(w, (char)c);
        else {
            unsigned cp = c;
            if (c >= 0xD800 && c < 0xDC00 && s[1] >= 0xDC00 && s[1] < 0xE000) {
                cp = 0x10000 + ((c - 0xD800) << 10) + (s[1] - 0xDC00);
                s++;
            }
            if (cp < 0x800) {
                wch(w, (char)(0xC0 | (cp >> 6)));
                wch(w, (char)(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                wch(w, (char)(0xE0 | (cp >> 12)));
                wch(w, (char)(0x80 | ((cp >> 6) & 0x3F)));
                wch(w, (char)(0x80 | (cp & 0x3F)));
            } else {
                wch(w, (char)(0xF0 | (cp >> 18)));
                wch(w, (char)(0x80 | ((cp >> 12) & 0x3F)));
                wch(w, (char)(0x80 | ((cp >> 6) & 0x3F)));
                wch(w, (char)(0x80 | (cp & 0x3F)));
            }
        }
    }
    wch(w, '"');
}

static void wkeyint(WBuf& w, int ind, const char* key, int v, bool last)
{
    windent(w, ind);
    wput(w, "\"");
    wput(w, key);
    wput(w, "\": ");
    wint(w, v);
    wput(w, last ? "\n" : ",\n");
}

void settingsSave()
{
    const int cap = 256 * 1024;
    WBuf w;
    w.b = (char*)HeapAlloc(GetProcessHeap(), 0, cap);
    w.n = 0;
    w.cap = cap;
    if (!w.b) return;

    wput(w, "{\n");
    wput(w, g_set.enabled ? "  \"Enabled\": true,\n" : "  \"Enabled\": false,\n");
    wkeyint(w, 1, "KelvinStep", g_set.kelvinStep, false);
    wkeyint(w, 1, "BrightnessStep", g_set.brightnessStep, false);
    wput(w, "  \"Monitors\": ");
    if (g_set.nmon == 0) wput(w, "{},\n");
    else {
        wput(w, "{\n");
        for (int i = 0; i < g_set.nmon; i++) {
            const MonSetting& m = g_set.mon[i];
            windent(w, 2);
            wstr(w, m.id);
            wput(w, ": {\n");
            wkeyint(w, 3, "Kelvin", m.kelvin, false);
            wkeyint(w, 3, "Brightness", m.brightness, false);
            wkeyint(w, 3, "FilterKelvin", m.filterKelvin, false);
            wkeyint(w, 3, "FilterReturnKelvin", m.filterReturn, false);
            windent(w, 3);
            wput(w, "\"Note\": ");
            wstr(w, m.note);
            wput(w, ",\n");
            windent(w, 3);
            if (m.np == 0) wput(w, "\"Presets\": []\n");
            else {
                wput(w, "\"Presets\": [\n");
                for (int p = 0; p < m.np; p++) {
                    windent(w, 4);
                    wput(w, "{\n");
                    wkeyint(w, 5, "Kelvin", m.presets[p].kelvin, false);
                    wkeyint(w, 5, "Brightness", m.presets[p].brightness, true);
                    windent(w, 4);
                    wput(w, p + 1 < m.np ? "},\n" : "}\n");
                }
                windent(w, 3);
                wput(w, "]\n");
            }
            windent(w, 2);
            wput(w, i + 1 < g_set.nmon ? "},\n" : "}\n");
        }
        wput(w, "  },\n");
    }
    wput(w, "  \"Hotkeys\": ");
    if (g_set.nhk == 0) wput(w, "{}\n");
    else {
        wput(w, "{\n");
        for (int i = 0; i < g_set.nhk; i++) {
            windent(w, 2);
            wstr(w, g_set.hk[i].action);
            wput(w, ": ");
            wstr(w, g_set.hk[i].keys);
            wput(w, i + 1 < g_set.nhk ? ",\n" : "\n");
        }
        wput(w, "  }\n");
    }
    wput(w, "}");

    wchar_t dir[MAX_PATH + 8], path[MAX_PATH + 32], tmp[MAX_PATH + 40];
    if (settingsPath(dir, MAX_PATH + 8, true) && settingsPath(path, MAX_PATH + 32, false)) {
        CreateDirectoryW(dir, NULL);
        wsprintfW(tmp, L"%s.tmp", path);
        HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD wrote = 0;
            BOOL ok = WriteFile(f, w.b, w.n, &wrote, NULL);
            CloseHandle(f);
            if (ok && wrote == (DWORD)w.n) MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
            else DeleteFileW(tmp);
        }
    }
    HeapFree(GetProcessHeap(), 0, w.b);
}

static bool readEdid(const wchar_t* iface, wchar_t* out)
{
    const wchar_t* p1 = wcschr(iface, L'#');
    if (!p1) return false;
    p1++;
    const wchar_t* e1 = wcschr(p1, L'#');
    if (!e1) return false;
    const wchar_t* p2 = e1 + 1;
    const wchar_t* e2 = wcschr(p2, L'#');
    wchar_t a[64], b[96];
    int la = (int)(e1 - p1), lb = e2 ? (int)(e2 - p2) : lstrlenW(p2);
    if (la <= 0 || la >= 64 || lb <= 0 || lb >= 96) return false;
    CopyMemory(a, p1, la * sizeof(wchar_t));
    a[la] = 0;
    CopyMemory(b, p2, lb * sizeof(wchar_t));
    b[lb] = 0;
    wchar_t path[320];
    wsprintfW(path, L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\%s\\%s\\Device Parameters", a, b);
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return false;
    BYTE edid[512];
    DWORD size = sizeof(edid), type = 0;
    LONG r = RegQueryValueExW(k, L"EDID", NULL, &type, edid, &size);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || type != REG_BINARY || size < 126) return false;
    for (int o = 54; o <= 108; o += 18) {
        if (edid[o] != 0 || edid[o + 1] != 0 || edid[o + 3] != 0xFC) continue;
        wchar_t t[16];
        int n = 0;
        for (int i = 0; i < 13; i++) {
            BYTE c = edid[o + 5 + i];
            if (c == '\n') break;
            t[n++] = c < 0x80 ? c : L'?';
        }
        int s = 0;
        while (s < n && (t[s] == ' ' || t[s] == '\t' || t[s] == '\r' || t[s] == 0)) s++;
        while (n > s && (t[n - 1] == ' ' || t[n - 1] == '\t' || t[n - 1] == '\r' || t[n - 1] == 0)) n--;
        if (n > s) {
            CopyMemory(out, t + s, (n - s) * sizeof(wchar_t));
            out[n - s] = 0;
            return true;
        }
    }
    return false;
}

static BOOL CALLBACK enumProc(HMONITOR h, HDC, LPRECT, LPARAM)
{
    if (g_nmon >= MAXMON) return TRUE;
    MONITORINFOEXW mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(h, &mi)) return TRUE;
    DISPLAY_DEVICEW dd;
    ZeroMemory(&dd, sizeof(dd));
    dd.cb = sizeof(dd);
    BOOL has = EnumDisplayDevicesW(mi.szDevice, 0, &dd, 1);
    Monitor& m = g_mon[g_nmon];
    ZeroMemory(&m, sizeof(m));
    m.h = h;
    copyStr(m.device, 40, mi.szDevice);
    copyStr(m.id, 200, (has && dd.DeviceID[0]) ? dd.DeviceID : mi.szDevice);
    if (!(has && readEdid(dd.DeviceID, m.name))) wsprintfW(m.name, L"Display %d", g_nmon + 1);
    m.bounds = mi.rcMonitor;
    m.work = mi.rcWork;
    m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    g_nmon++;
    return TRUE;
}

void monitorsRefresh()
{
    g_nmon = 0;
    EnumDisplayMonitors(NULL, NULL, enumProc, 0);
    for (int i = 1; i < g_nmon; i++) {
        for (int j = i; j > 0; j--) {
            const RECT& a = g_mon[j - 1].bounds;
            const RECT& b = g_mon[j].bounds;
            bool swap = a.left > b.left || (a.left == b.left && a.top > b.top);
            if (!swap) break;
            Monitor t = g_mon[j - 1];
            g_mon[j - 1] = g_mon[j];
            g_mon[j] = t;
        }
    }
}

static void rawKelvin(int kelvin, double* o)
{
    double t = kelvin / 100.0;
    if (t < 1.0) t = 1.0;
    double r, g, b;
    if (t <= 66) {
        r = 255;
        g = 99.4708025861 * log(t) - 161.1195681661;
        b = t <= 19 ? 0 : 138.5177312231 * log(t - 10) - 305.0447927307;
    } else {
        r = 329.698727446 * pow(t - 60, -0.1332047592);
        g = 288.1221695283 * pow(t - 60, -0.0755148492);
        b = 255;
    }
    o[0] = r < 0 ? 0 : (r > 255 ? 255 : r);
    o[1] = g < 0 ? 0 : (g > 255 ? 255 : g);
    o[2] = b < 0 ? 0 : (b > 255 ? 255 : b);
}

void fromKelvin(int kelvin, double* out)
{
    double c[3], w[3];
    rawKelvin(kelvin, c);
    rawKelvin(6500, w);
    double r = c[0] / w[0], g = c[1] / w[1], b = c[2] / w[2];
    double mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (mx > 1) { r /= mx; g /= mx; b /= mx; }
    out[0] = r; out[1] = g; out[2] = b;
}

static void rampFill(unsigned short* ramp, double r, double g, double b)
{
    for (int i = 0; i < 256; i++) {
        double v = i * 257.0;
        double x = v * r, y = v * g, z = v * b;
        ramp[i] = (unsigned short)(x < 0 ? 0 : (x > 65535 ? 65535 : x));
        ramp[256 + i] = (unsigned short)(y < 0 ? 0 : (y > 65535 ? 65535 : y));
        ramp[512 + i] = (unsigned short)(z < 0 ? 0 : (z > 65535 ? 65535 : z));
    }
}

static void rampBuild(int mi)
{
    if (!g_set.enabled) { rampFill(g_mon[mi].ramp, 1, 1, 1); return; }
    MonSetting* s = ctlGet(mi);
    double c[3];
    fromKelvin(s->kelvin, c);
    double level = s->brightness / 100.0;
    rampFill(g_mon[mi].ramp, c[0] * level, c[1] * level, c[2] * level);
}

static void rampPush(int mi)
{
    HDC dc = CreateDCW(NULL, g_mon[mi].device, NULL, NULL);
    if (!dc) return;
    SetDeviceGammaRamp(dc, g_mon[mi].ramp);
    DeleteDC(dc);
}

static void queueSave()
{
    if (g_main) SetTimer(g_main, TIMER_SAVE, 600, NULL);
}

MonSetting* ctlGet(int mi)
{
    const wchar_t* id = g_mon[mi].id;
    for (int i = 0; i < g_set.nmon; i++)
        if (lstrcmpW(g_set.mon[i].id, id) == 0) return &g_set.mon[i];
    if (g_set.nmon >= MAXSET) return &g_set.mon[MAXSET - 1];
    MonSetting* m = &g_set.mon[g_set.nmon++];
    monDefaults(m);
    copyStr(m->id, 200, id);
    monNormalize(m);
    return m;
}

static void apply(int mi)
{
    rampBuild(mi);
    rampPush(mi);
}

void ctlApplyAll()
{
    for (int i = 0; i < g_nmon; i++) apply(i);
}

void ctlRampsApply()
{
    for (int i = 0; i < g_nmon; i++) rampPush(i);
}

void ctlRestoreAll()
{
    for (int i = 0; i < g_nmon; i++) {
        rampFill(g_mon[i].ramp, 1, 1, 1);
        rampPush(i);
    }
}

void ctlSet(int mi, int kelvin, int brightness)
{
    MonSetting* s = ctlGet(mi);
    if (kelvin != NOVAL) s->kelvin = clampi(kelvin, 1200, 9000);
    if (brightness != NOVAL) s->brightness = clampi(brightness, 10, 100);
    apply(mi);
    queueSave();
    uiChanged();
}

void ctlSetEnabled(bool on)
{
    if (g_set.enabled == on) return;
    g_set.enabled = on;
    ctlApplyAll();
    queueSave();
    uiChanged();
}

void ctlResetAll()
{
    for (int i = 0; i < g_nmon; i++) {
        MonSetting* s = ctlGet(i);
        s->kelvin = 6500;
        s->brightness = 100;
    }
    ctlApplyAll();
    queueSave();
    uiChanged();
}

void ctlApplyPreset(int mi, int index)
{
    Preset p = ctlGet(mi)->presets[index];
    ctlSet(mi, p.kelvin, p.brightness);
}

void ctlSavePreset(int mi, int index)
{
    MonSetting* s = ctlGet(mi);
    s->presets[index].kelvin = s->kelvin;
    s->presets[index].brightness = s->brightness;
    queueSave();
    uiChanged();
}

void ctlSetNote(int mi, const wchar_t* note)
{
    wchar_t t[128];
    copyStr(t, 128, note);
    int n = lstrlenW(t), s = 0;
    while (s < n && (t[s] == ' ' || t[s] == '\t' || t[s] == '\r' || t[s] == '\n')) s++;
    while (n > s && (t[n - 1] == ' ' || t[n - 1] == '\t' || t[n - 1] == '\r' || t[n - 1] == '\n')) n--;
    MonSetting* m = ctlGet(mi);
    CopyMemory(m->note, t + s, (n - s) * sizeof(wchar_t));
    m->note[n - s] = 0;
    queueSave();
    uiChanged();
}

bool ctlPresetActive(int mi, int index)
{
    MonSetting* s = ctlGet(mi);
    return s->presets[index].kelvin == s->kelvin && s->presets[index].brightness == s->brightness;
}

bool ctlFilterActive(int mi)
{
    MonSetting* s = ctlGet(mi);
    return s->kelvin == s->filterKelvin;
}

void ctlToggleFilter(int mi)
{
    MonSetting* s = ctlGet(mi);
    if (ctlFilterActive(mi)) {
        ctlSet(mi, s->filterReturn, NOVAL);
        return;
    }
    s->filterReturn = s->kelvin;
    ctlSet(mi, s->filterKelvin, NOVAL);
}

void ctlSaveFilter(int mi)
{
    MonSetting* s = ctlGet(mi);
    s->filterKelvin = s->kelvin;
    queueSave();
    uiChanged();
}

int ctlMonitorUnderCursor()
{
    if (g_nmon == 0) return -1;
    POINT pt;
    if (GetCursorPos(&pt)) {
        HMONITOR h = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        for (int i = 0; i < g_nmon; i++)
            if (g_mon[i].h == h) return i;
    }
    for (int i = 0; i < g_nmon; i++)
        if (g_mon[i].primary) return i;
    return 0;
}

void ctlFlush()
{
    if (g_main) KillTimer(g_main, TIMER_SAVE);
    settingsSave();
}

struct KeyName { const wchar_t* name; UINT vk; };

static const KeyName kKeys[] = {
    { L"Up", 0x26 }, { L"Down", 0x28 }, { L"Left", 0x25 }, { L"Right", 0x27 },
    { L"Home", 0x24 }, { L"End", 0x23 }, { L"PageUp", 0x21 }, { L"Prior", 0x21 },
    { L"PageDown", 0x22 }, { L"Next", 0x22 }, { L"Insert", 0x2D }, { L"Delete", 0x2E },
    { L"Space", 0x20 }, { L"Tab", 0x09 }, { L"Enter", 0x0D }, { L"Return", 0x0D },
    { L"Escape", 0x1B }, { L"Back", 0x08 }, { L"Add", 0x6B }, { L"Subtract", 0x6D },
    { L"Multiply", 0x6A }, { L"Divide", 0x6F }, { L"Decimal", 0x6E },
    { L"OemPlus", 0xBB }, { L"OemMinus", 0xBD }, { L"OemComma", 0xBC }, { L"OemPeriod", 0xBE },
    { L"OemTilde", 0xC0 }, { L"OemOpenBrackets", 0xDB }, { L"OemCloseBrackets", 0xDD },
    { L"OemPipe", 0xDC }, { L"OemQuotes", 0xDE }, { L"OemSemicolon", 0xBA }, { L"OemQuestion", 0xBF },
    { L"PrintScreen", 0x2C }, { L"Snapshot", 0x2C }, { L"Pause", 0x13 }, { L"Scroll", 0x91 },
    { L"Capital", 0x14 }, { L"CapsLock", 0x14 }, { L"NumLock", 0x90 },
};

static bool keyVk(const wchar_t* t, UINT* vk)
{
    int n = lstrlenW(t);
    if (n == 1) {
        wchar_t c = t[0];
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        if (c >= 'A' && c <= 'Z') { *vk = c; return true; }
    }
    if (n == 2 && (t[0] == 'D' || t[0] == 'd') && t[1] >= '0' && t[1] <= '9') { *vk = t[1]; return true; }
    if (n == 7 && !_wcsnicmp(t, L"NumPad", 6) && t[6] >= '0' && t[6] <= '9') { *vk = 0x60 + (t[6] - '0'); return true; }
    if ((t[0] == 'F' || t[0] == 'f') && n >= 2 && n <= 3) {
        int v = 0;
        for (int i = 1; i < n; i++) {
            if (t[i] < '0' || t[i] > '9') { v = 0; break; }
            v = v * 10 + (t[i] - '0');
        }
        if (v >= 1 && v <= 24) { *vk = 0x70 + v - 1; return true; }
    }
    for (size_t i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++)
        if (lstrcmpiW(t, kKeys[i].name) == 0) { *vk = kKeys[i].vk; return true; }
    return false;
}

static bool parseKeys(const wchar_t* text, UINT* mods, UINT* vk)
{
    *mods = 0;
    *vk = 0;
    const wchar_t* p = text;
    for (;;) {
        wchar_t tok[48];
        int n = 0;
        while (*p && *p != '+') { if (n < 47) tok[n++] = *p; p++; }
        tok[n] = 0;
        int s = 0;
        while (s < n && tok[s] <= ' ') s++;
        while (n > s && tok[n - 1] <= ' ') n--;
        tok[n] = 0;
        const wchar_t* t = tok + s;
        if (n > s) {
            if (lstrcmpiW(t, L"alt") == 0) *mods |= 1;
            else if (lstrcmpiW(t, L"ctrl") == 0 || lstrcmpiW(t, L"control") == 0) *mods |= 2;
            else if (lstrcmpiW(t, L"shift") == 0) *mods |= 4;
            else if (lstrcmpiW(t, L"win") == 0) *mods |= 8;
            else if (!keyVk(t, vk)) return false;
        }
        if (!*p) break;
        p++;
    }
    return *vk != 0;
}

void hotkeysRegister(HWND hwnd)
{
    g_nfailed = 0;
    for (int i = 0; i < g_set.nhk; i++) {
        const wchar_t* k = g_set.hk[i].keys;
        const wchar_t* q = k;
        while (*q && *q <= ' ') q++;
        if (!*q) continue;
        UINT mods, vk;
        if (!(parseKeys(k, &mods, &vk) && RegisterHotKey(hwnd, i + 1, mods, vk)))
            copyStr(g_failed[g_nfailed++], 64, k);
    }
}

void hotkeysUnregister(HWND hwnd)
{
    for (int i = 0; i < g_set.nhk; i++) UnregisterHotKey(hwnd, i + 1);
}

static const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool autostartGet()
{
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"Dusk", RRF_RT_ANY, NULL, NULL, NULL) == ERROR_SUCCESS;
}

void autostartSet(bool on)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH], val[MAX_PATH + 16];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        wsprintfW(val, L"\"%s\" /tray", exe);
        RegSetValueExW(k, L"Dusk", 0, REG_SZ, (const BYTE*)val, (lstrlenW(val) + 1) * sizeof(wchar_t));
    } else {
        RegDeleteValueW(k, L"Dusk");
    }
    RegCloseKey(k);
}
