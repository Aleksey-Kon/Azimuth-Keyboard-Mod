@echo off
setlocal
for /f "usebackq tokens=*" %%v in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "GAMEPAD_VS=%%v"
if not defined GAMEPAD_VS (
    echo Visual Studio C++ x64 build tools were not found.
    exit /b 1
)
call "%GAMEPAD_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
pushd "%~dp0"
if not exist ..\data\gamepad\obj mkdir ..\data\gamepad\obj
if not exist ..\data\gamepad\tests mkdir ..\data\gamepad\tests
if not exist ..\data\gamepad\presets mkdir ..\data\gamepad\presets
for %%p in (01-wasd.ini 02-arrows.ini 03-smooth.ini) do if not exist "..\data\gamepad\presets\%%p" copy /y "presets\%%p" "..\data\gamepad\presets\%%p" >nul
if not exist ..\data\gamepad\settings.ini copy /y settings.ini ..\data\gamepad\settings.ini >nul
cl /nologo /std:c++17 /EHsc /O2 /MT /utf-8 /W4 /LD GamepadMod.cpp LauncherMenu.cpp /Fo:..\data\gamepad\obj\ /Fe:..\data\gamepad\GamepadMod.dll /link /IMPLIB:..\data\gamepad\obj\GamepadMod.lib windowsapp.lib user32.lib shell32.lib comctl32.lib gdi32.lib
if errorlevel 1 goto failed
ml64 /nologo /c /Fo ..\data\gamepad\obj\Bootstrap.obj Bootstrap.asm
if errorlevel 1 goto failed
cl /nologo /std:c++17 /EHsc /O2 /MT /utf-8 /W4 Launcher.cpp ..\data\gamepad\obj\Bootstrap.obj /Fo:..\data\gamepad\obj\Launcher.obj /Fe:..\Start-Azimuth-Gamepad.exe /link /SUBSYSTEM:WINDOWS user32.lib
if errorlevel 1 goto failed
cl /nologo /std:c++17 /EHsc /O2 /MT /utf-8 /W4 PresetTests.cpp /Fo:..\data\gamepad\obj\PresetTests.obj /Fe:..\data\gamepad\tests\PresetTests.exe
if errorlevel 1 goto failed
..\data\gamepad\tests\PresetTests.exe presets
if errorlevel 1 goto failed
cl /nologo /std:c++17 /EHsc /O2 /MT /utf-8 /W4 IntegrationTests.cpp /Fo:..\data\gamepad\obj\IntegrationTests.obj /Fe:..\data\gamepad\tests\IntegrationTests.exe /link windowsapp.lib user32.lib gdi32.lib
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
