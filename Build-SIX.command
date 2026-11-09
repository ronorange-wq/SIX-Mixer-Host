#!/bin/bash
# Double-click (right-click > Open the first time) to build and install SIX.
cd "$(dirname "$0")"
LOG="$HOME/Desktop/SIX-build-log.txt"
exec > >(tee "$LOG") 2>&1

echo "=============================================="
echo "  SIX - Mixer Host: build & install"
echo "=============================================="

# 1. Apple developer tools (compiler + git)
if ! xcode-select -p >/dev/null 2>&1; then
    echo
    echo ">> macOS needs Apple's free developer tools first."
    echo ">> A window will open: click 'Install' and wait until it finishes."
    echo ">> Then open Build-SIX.command again."
    xcode-select --install
    read -n 1 -s -r -p "Press any key to close"
    exit 0
fi

# 2. CMake (downloaded locally if not installed - nothing system-wide)
if ! command -v cmake >/dev/null 2>&1; then
    if [ ! -x ".tools/CMake.app/Contents/bin/cmake" ]; then
        echo ">> Downloading CMake..."
        mkdir -p .tools
        curl -L --fail -o .tools/cmake.tar.gz \
            https://github.com/Kitware/CMake/releases/download/v3.30.5/cmake-3.30.5-macos-universal.tar.gz || {
            echo "Download failed - check the internet connection and try again."; read -n 1 -s -r; exit 1; }
        tar -xzf .tools/cmake.tar.gz -C .tools
        mv .tools/cmake-3.30.5-macos-universal/CMake.app .tools/CMake.app
        rm -rf .tools/cmake.tar.gz .tools/cmake-3.30.5-macos-universal
    fi
    export PATH="$PWD/.tools/CMake.app/Contents/bin:$PATH"
fi

chmod +x build_mac.sh
echo ">> Building SIX (about 10-15 minutes the first time)..."
if ./build_mac.sh; then
    echo
    echo "=============================================="
    echo "  DONE. SIX is installed."
    echo "  In Ableton Live: Settings > Plug-Ins > Rescan"
    echo "=============================================="
    open "$HOME/Library/Audio/Plug-Ins/VST3"
else
    echo
    echo "!! Something went wrong. Please send the file SIX-build-log.txt (on your Desktop) to Claude."
fi
read -n 1 -s -r -p "Press any key to close"
