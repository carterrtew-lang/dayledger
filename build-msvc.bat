@echo off
rem Native Windows builds (x64 Native Tools Command Prompt for VS 2022).
rem   build-msvc.bat            -> build\TimeTracker.exe
rem   build-msvc.bat legacy     -> build\pomodoro.exe (CRT-free)
rem   build-msvc.bat legacy crt -> build\pomodoro-crt.exe
rem NOTE: this script follows the spec but could not be tested on the authoring machine
rem       (no MSVC / Windows SDK installed there); only the MinGW build was verified.
setlocal
cd /d "%~dp0"
if not exist build mkdir build
rc /nologo /fo build\res.res src\resource.rc || exit /b 1
if /i "%1"=="legacy" goto legacy
cl /nologo /O2 /EHsc /MT /utf-8 /W4 /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN ^
   /Fobuild\time_tracker.obj /Febuild\TimeTracker.exe src\time_tracker.cpp build\res.res ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO user32.lib gdi32.lib comdlg32.lib comctl32.lib shell32.lib ole32.lib uuid.lib || exit /b 1
exit /b 0

:legacy
shift
if /i "%1"=="crt" goto crt

rem (1) MSVC CRT-free  -> ~9-13 KB
cl /nologo /c /O1 /Os /Oi /GS- /Gs65536 /Gy /Gw /GR- /EHs-c- /Zl /Zc:threadSafeInit- /utf-8 /W4 /std:c++17 ^
   /DNOCRT /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /Fobuild\main.obj src\main.cpp || exit /b 1
link /nologo /NODEFAULTLIB /ENTRY:Entry /SUBSYSTEM:WINDOWS,6.0 /OPT:REF /OPT:ICF /INCREMENTAL:NO /MANIFEST:NO ^
     /MERGE:.rdata=.text /FILEALIGN:512 /DYNAMICBASE /NXCOMPAT /RELEASE /Brepro ^
     /OUT:build\pomodoro.exe build\main.obj build\res.res kernel32.lib user32.lib gdi32.lib || exit /b 1
goto done

:crt
rem (2) MSVC with static CRT -> ~110-160 KB
cl /nologo /c /O1 /Os /GR- /EHsc /MT /utf-8 /W4 /std:c++17 ^
   /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /Fobuild\main.obj src\main.cpp || exit /b 1
link /nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /INCREMENTAL:NO /MANIFEST:NO /RELEASE ^
     /OUT:build\pomodoro-crt.exe build\main.obj build\res.res user32.lib gdi32.lib || exit /b 1

:done
for %%F in (build\pomodoro*.exe) do echo %%F %%~zF bytes
rem Never add /ALIGN:16 (legacy loader path, AV false positives) or /MERGE:.pdata=.text (x64 unwind info must stay valid).
