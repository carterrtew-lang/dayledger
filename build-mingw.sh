#!/bin/sh
# Native Windows build (MinGW-w64): ./build-mingw.sh -> build/TimeTracker.exe (DayLedger)
# Environment: CXX (default g++), WINDRES (default windres), USE_ZIG_RC=1 to use "zig rc".
# Cross-compiling from Linux works too, e.g.
#   CXX=x86_64-w64-mingw32-g++-posix WINDRES=x86_64-w64-mingw32-windres ./build-mingw.sh
# Run from the project directory (the script cd's to its own location first).
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
WINDRES=${WINDRES:-windres}
mkdir -p build

if [ -n "$USE_ZIG_RC" ]; then
  zig rc src/time_tracker.rc -o build/time_tracker_res.res; RES=build/time_tracker_res.res
else
  # -I src: the .rc file references its icon and manifest relative to its own directory
  "$WINDRES" -I src -O coff -o build/time_tracker_res.o src/time_tracker.rc; RES=build/time_tracker_res.o
fi

# GCC's endfile spec links the toolchain's default-manifest.o whenever that file exists, and ld then
# reports ".rsrc merge failure: multiple non-default manifests" against ours (non-fatal, ours still
# wins). To keep the link clean, skip the implicit start/end files and name them explicitly so only
# src/app.manifest is embedded. Toolchains without that file use the plain line.
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
