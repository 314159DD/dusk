#pragma once
#include <windows.h>

#define MAXMON 16
#define MAXSET 32
#define MAXHK 24
#define NOVAL (-1000000)
#define TIMER_KEEP 1
#define TIMER_SAVE 2
#define WM_TRAY (WM_APP + 1)

struct Preset { int kelvin; int brightness; };

struct MonSetting {
    wchar_t id[200];
    int kelvin, brightness, filterKelvin, filterReturn;
    wchar_t note[128];
    int np;
    Preset presets[8];
};

struct HotkeyDef { wchar_t action[32]; wchar_t keys[64]; };

struct Settings {
    bool enabled;
    int kelvinStep, brightnessStep;
    int nmon;
    MonSetting mon[MAXSET];
    int nhk;
    HotkeyDef hk[MAXHK];
};

struct Monitor {
    HMONITOR h;
    wchar_t device[40];
    wchar_t id[200];
    wchar_t name[64];
    RECT bounds, work;
    bool primary;
    unsigned short ramp[768];
};

extern Settings g_set;
extern Monitor g_mon[MAXMON];
extern int g_nmon;
extern HWND g_main;
extern HINSTANCE g_inst;
extern wchar_t g_failed[MAXHK][64];
extern int g_nfailed;

void settingsLoad();
void settingsSave();
void monitorsRefresh();
void fromKelvin(int kelvin, double* out);

MonSetting* ctlGet(int mi);
void ctlSet(int mi, int kelvin, int brightness);
void ctlSetEnabled(bool on);
void ctlApplyAll();
void ctlRampsApply();
void ctlRestoreAll();
void ctlResetAll();
void ctlApplyPreset(int mi, int index);
void ctlSavePreset(int mi, int index);
void ctlSetNote(int mi, const wchar_t* note);
bool ctlPresetActive(int mi, int index);
bool ctlFilterActive(int mi);
void ctlToggleFilter(int mi);
void ctlSaveFilter(int mi);
int ctlMonitorUnderCursor();
void ctlFlush();

void hotkeysRegister(HWND hwnd);
void hotkeysUnregister(HWND hwnd);

bool autostartGet();
void autostartSet(bool on);

void uiChanged();
void uiRebuild();
void uiShowPanel();
void uiTogglePanel();
void uiClosePanel();
void uiFlash(int mi, const wchar_t* name, const wchar_t* value);
void uiShutdown();
bool uiSelfTest(const wchar_t* pngPath);
