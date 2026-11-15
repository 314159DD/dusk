@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS (
  echo Visual Studio C++ build tools not found
  exit /b 1
)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist obj mkdir obj
if not exist dist mkdir dist
rc /nologo /fo obj\dusk.res src\dusk.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /O1 /GL /GS- /Gy /Gw /MT /GR- /EHs-c- /W3 /DUNICODE /D_UNICODE /D_HAS_EXCEPTIONS=0 /Fo:obj\ /Fe:dist\Dusk.exe src\core.cpp src\ui.cpp src\main.cpp /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /DELAYLOAD:gdiplus.dll /DELAY:UNLOAD obj\dusk.res user32.lib gdi32.lib shell32.lib advapi32.lib gdiplus.lib delayimp.lib
if errorlevel 1 exit /b 1
echo built dist\Dusk.exe
