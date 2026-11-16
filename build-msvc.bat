@echo off
rem Native Windows build (x64 Native Tools Command Prompt for VS 2022):
rem   build-msvc.bat -> build\TimeTracker.exe (DayLedger)
rem NOTE: this script follows the spec but could not be tested on the authoring machine
rem       (no MSVC / Windows SDK installed there); only the MinGW build was verified.
setlocal
cd /d "%~dp0"
if not exist build mkdir build
rc /nologo /fo build\time_tracker.res src\time_tracker.rc || exit /b 1
cl /nologo /O2 /EHsc /MT /utf-8 /W4 /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN ^
   /Fobuild\time_tracker.obj /Febuild\TimeTracker.exe src\time_tracker.cpp build\time_tracker.res ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO user32.lib gdi32.lib gdiplus.lib comdlg32.lib comctl32.lib ^
   shell32.lib ole32.lib uuid.lib advapi32.lib || exit /b 1
exit /b 0
