#!/bin/sh
# Native Windows builds (MinGW-w64).
#   ./build-mingw.sh            -> build/TimeTracker.exe (DayLedger)
#   ./build-mingw.sh legacy     -> build/pomodoro.exe (CRT-free)
#   CRT=1 ./build-mingw.sh legacy -> build/pomodoro-crt.exe
# Environment: CXX (default g++), WINDRES (default windres), USE_ZIG_RC=1 to use "zig rc".
# Cross-compiling from Linux works too, e.g.
#   CXX=x86_64-w64-mingw32-g++-posix WINDRES=x86_64-w64-mingw32-windres ./build-mingw.sh
# Run from the project directory (the script cd's to its own location first).
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
WINDRES=${WINDRES:-windres}
mkdir -p build

# compile_rc <script> <basename>: sets RES to the compiled resource object.
compile_rc() {
  if [ -n "$USE_ZIG_RC" ]; then
    zig rc "$1" -o "build/$2.res"; RES="build/$2.res"
  else
    # -I src: the .rc files reference their icon and manifest relative to their own directory
    "$WINDRES" -I src -O coff -o "build/$2.o" "$1"; RES="build/$2.o"
  fi
}

if [ "$1" != "legacy" ]; then
  compile_rc src/time_tracker.rc time_tracker_res
  DM=$($CXX -print-file-name=default-manifest.o 2>/dev/null || true)
  STARTFILES=""; ENDFILES=""
  if [ -n "$DM" ] && [ -f "$DM" ]; then
    STARTFILES="-nostartfiles $($CXX -print-file-name=crt2.o) $($CXX -print-file-name=crtbegin.o)"
    ENDFILES="$($CXX -print-file-name=crtend.o)"
  fi
  "$CXX" -std=c++17 -O2 -s -static -mwindows -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN \
       -Wall -Wextra $STARTFILES -o build/TimeTracker.exe src/time_tracker.cpp $RES \
       -luser32 -lgdi32 -lgdiplus -lcomdlg32 -lcomctl32 -lshell32 -lole32 -luuid -ladvapi32 $ENDFILES
  ls -l build/TimeTracker.exe
  exit 0
fi

compile_rc src/resource.rc res

if [ "$CRT" = "1" ]; then
  # (4) MinGW-w64 with CRT (static libgcc/libstdc++, links the system CRT DLL) -> ~30-80 KB
  # GCC's endfile spec links the toolchain's default-manifest.o whenever that file exists, and ld
  # (binutils 2.47) then prints ".rsrc merge failure: multiple non-default manifests" against ours
  # (non-fatal, ours still wins). To keep the link clean, skip the implicit start/end files and name
  # them explicitly so only src/app.manifest is embedded. Toolchains without that file use the plain line.
  DM=$($CXX -print-file-name=default-manifest.o 2>/dev/null || true)
  STARTFILES=""; ENDFILES=""
  if [ -n "$DM" ] && [ -f "$DM" ]; then
    STARTFILES="-nostartfiles $($CXX -print-file-name=crt2.o) $($CXX -print-file-name=crtbegin.o)"
    ENDFILES="$($CXX -print-file-name=crtend.o)"
  fi
  $CXX -std=c++17 -Os -s -static -mwindows -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -Wall \
       -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables -fno-unwind-tables \
       -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,--no-insert-timestamp \
       $STARTFILES -o build/pomodoro-crt.exe src/main.cpp $RES -luser32 -lgdi32 $ENDFILES
else
  # (3) MinGW-w64 CRT-free -> ~10-15 KB; imports only kernel32/user32/gdi32
  # (no -fdata-sections here: with it ld puts the zero-initialised state struct in .data instead of .bss, +512 B)
  $CXX -std=c++17 -Os -s -DNOCRT -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -Wall \
       -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables -fno-unwind-tables \
       -fno-stack-protector -fno-threadsafe-statics -fno-ident -fno-builtin -fno-tree-loop-distribute-patterns \
       -mno-stack-arg-probe -ffunction-sections \
       -nostdlib -nostartfiles -Wl,--gc-sections -Wl,-e,Entry -Wl,--subsystem,windows \
       -Wl,--no-insert-timestamp -Wl,--file-alignment=512 \
       -o build/pomodoro.exe src/main.cpp $RES -lkernel32 -luser32 -lgdi32
fi
ls -l build/pomodoro*.exe
