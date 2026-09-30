@echo off
rem Build with Microsoft's compiler. Open "x64 Native Tools Command Prompt for VS"
rem and run this from the folder it lives in.
setlocal
where cl >nul 2>nul || (echo Run this from an x64 Native Tools Command Prompt for VS. & exit /b 1)
if not exist build mkdir build
rc /nologo /fo build\app.res app.rc || exit /b 1
set IM=third_party\imgui
cl /nologo /std:c++17 /O2 /EHsc /MT /DNDEBUG ^
 /DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD /DIMGUI_IMPL_WIN32_DISABLE_LINKING_XINPUT /DIMGUI_DISABLE_DEMO_WINDOWS ^
 /Isrc /I%IM% /I%IM%\backends ^
 src\main.cpp src\gui.cpp src\engine.cpp src\input.cpp src\outputs.cpp src\mapping.cpp ^
 %IM%\imgui.cpp %IM%\imgui_draw.cpp %IM%\imgui_tables.cpp %IM%\imgui_widgets.cpp ^
 %IM%\backends\imgui_impl_win32.cpp %IM%\backends\imgui_impl_opengl3.cpp ^
 /Fobuild\ /Febuild\ClaudeController.exe ^
 /link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup build\app.res ^
 opengl32.lib gdi32.lib user32.lib shell32.lib dwmapi.lib imm32.lib winmm.lib hid.lib setupapi.lib comdlg32.lib ole32.lib
if errorlevel 1 exit /b 1
echo.
echo built build\ClaudeController.exe
