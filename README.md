<p align="center">
  <img src="brand/readme-header.png" alt="Dusk" width="100%">
</p>

<h1 align="center">Dusk</h1>

<p align="center">
  <b>Colour temperature and brightness for Windows, per monitor.</b>
</p>

<p align="center">
  <a href="https://github.com/314159DD/dusk/releases/latest"><img src="https://img.shields.io/badge/release-v0.1.1-8FA2FF?style=flat-square" alt="Release"></a>
  <img src="https://img.shields.io/badge/Windows-10%20%7C%2011-2B3A8C?style=flat-square" alt="Windows 10 and 11">
  <img src="https://img.shields.io/badge/exe-223%20KB-2B3A8C?style=flat-square" alt="223 KB">
  <img src="https://img.shields.io/badge/idle%20RAM-2%20MB-2B3A8C?style=flat-square" alt="2 MB idle">
  <img src="https://img.shields.io/badge/runtime-none-1A1E27?style=flat-square" alt="No runtime">
  <img src="https://img.shields.io/badge/network-none-1A1E27?style=flat-square" alt="No network access">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-1A1E27?style=flat-square" alt="MIT"></a>
</p>

<p align="center">
  <a href="https://github.com/314159DD/dusk/releases/latest"><img src="https://img.shields.io/badge/Download-Dusk.exe-8FA2FF?style=for-the-badge&labelColor=8FA2FF" alt="Download"></a>
</p>

---

Different monitors rarely look the same at the same setting, so a multi-monitor desk often needs an individual brightness and colour temperature for each screen. Dusk lets you set every monitor on its own, by hotkey, and keeps it there. It has no schedule, no location lookup and no account.

<p align="center">
  <img src="brand/panel.png" alt="The Dusk panel" width="380">
</p>

## Features

- **Per monitor.** One temperature and one brightness slider for every screen, remembered per physical display.
- **Hotkeys first.** Change the screen under your mouse without opening anything.
- **Three presets per screen.** Press the disk, pick 1, 2 or 3.
- **Blue light filter.** One click per screen, with its own stored temperature.
- **Quiet readout.** A small popup in the top right corner of the changed screen shows the new values for a second, then goes away.
- **Notes.** Click beside a monitor name to label it.
- **Your order.** Drag a screen by its grip to rearrange the list.
- **Tiny.** One native exe, about 2 MB of memory while idle, nothing to install, zero network calls.

## Hotkeys

| Keys | Action |
| --- | --- |
| `Alt` `PageUp` / `PageDown` | Brighter / darker |
| `Alt` `Shift` `PageUp` / `PageDown` | Cooler / warmer |
| add `Ctrl` | Apply to all screens |
| `Ctrl` `Alt` `Home` | Everything on or off |

Hotkeys act on the screen under the mouse.

## Install

1. Download `Dusk.exe` from [Releases](https://github.com/314159DD/dusk/releases/latest).
2. Run it. It lives in the tray.
3. Optional: switch on **Start with Windows** in the panel.

Quit other screen tint tools first, they overwrite each other.

<details>
<summary><b>Build from source</b></summary>

Needs the Visual Studio C++ build tools and a Windows SDK.

```
build.bat
```

The result is `dist\Dusk.exe`.
</details>

<details>
<summary><b>Good to know</b></summary>

- Brightness is software dimming. It darkens the picture, not the backlight.
- Very warm or very dim values need the registry value `GdiICMGammaRange` (DWORD 256) under `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ICM`, then a restart.
- Keys and step sizes live in `%APPDATA%\Dusk\settings.json`.
</details>

---

<p align="center">MIT licensed.</p>
