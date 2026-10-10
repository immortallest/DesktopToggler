#!/usr/bin/env bash
# Builds DesktopToggler.exe on Linux/macOS/WSL with the mingw-w64 cross compiler.
#   Debian/Ubuntu:  sudo apt-get install g++-mingw-w64-x86-64
# Output: dist/DesktopToggler.exe, dist/DesktopToggler-Uninstall.exe, dist/SHA256SUMS.txt
set -euo pipefail
cd "$(dirname "$0")"

CXX=x86_64-w64-mingw32-g++
RC=x86_64-w64-mingw32-windres
STRIP=x86_64-w64-mingw32-strip
OBJDUMP=x86_64-w64-mingw32-objdump

rm -rf build dist
mkdir -p build dist

echo "==> Compiling resources (manifest + version info)"
( cd src && "$RC" -O coff -i DesktopToggler.rc -o ../build/res.o )

echo "==> Compiling DesktopToggler.exe"
( cd src && "$CXX" -O2 -municode -mwindows -std=c++17 \
      -Wall -Wextra -Wno-unused-parameter -static \
      -o ../build/DesktopToggler.exe DesktopToggler.cpp ../build/res.o \
      -luser32 -lshell32 -ladvapi32 -lole32 -lshlwapi )
"$STRIP" build/DesktopToggler.exe

echo "==> Checking imports (must be plain Windows system DLLs only)"
bad=0
while read -r dll; do
  case "${dll,,}" in
    advapi32.dll|kernel32.dll|msvcrt.dll|ole32.dll|shell32.dll|shlwapi.dll|user32.dll) ;;
    *) echo "unexpected DLL dependency: $dll"; bad=1 ;;
  esac
done < <("$OBJDUMP" -p build/DesktopToggler.exe | awk '/DLL Name:/ {print $3}')
[ "$bad" -eq 0 ] || { echo "FAILED: unexpected runtime dependency"; exit 1; }

echo "==> Packaging"
cp build/DesktopToggler.exe dist/DesktopToggler.exe
# The uninstaller is the very same program: a file name containing "uninstall"
# selects uninstall mode.
cp build/DesktopToggler.exe dist/DesktopToggler-Uninstall.exe
( cd dist && sha256sum DesktopToggler.exe DesktopToggler-Uninstall.exe > SHA256SUMS.txt )
ls -l dist
echo "Done."
