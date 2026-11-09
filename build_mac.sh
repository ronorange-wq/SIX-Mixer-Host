#!/bin/bash
# SIX - Mixer Host: build SIX.vst3 (Release, Apple Silicon), run the tests, install for Ableton Live.
#   ./build_mac.sh              build + tests + install
#   SKIP_TESTS=1 ./build_mac.sh build + install only
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"

command -v cmake >/dev/null || { echo "CMake is missing. Install it with:  brew install cmake   (or from cmake.org)"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "Xcode Command Line Tools are missing. Run:  xcode-select --install"; exit 1; }

BUILD="$ROOT/build-mac"
GEN=()
command -v ninja >/dev/null && GEN=(-G Ninja)

cmake -S . -B "$BUILD" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DSIX_BUILD_TESTS=ON
cmake --build "$BUILD" --config Release -j "$(sysctl -n hw.ncpu)" \
      --target SIX_VST3 SIXTests SIXTestComp_VST3 SIXTestLatency_VST3 SIXTestWidener_VST3

TESTS="skipped"
if [ "${SKIP_TESTS:-0}" != "1" ]; then
    T="$BUILD/SIXTests_artefacts/Release/SIXTests"
    TESTS="passed"
    echo "== Engine / hosting tests";               "$T" "$BUILD"            || TESTS="FAILED (see above)"
    echo "== UI tests (drag & drop, editor windows)"; "$T" "$BUILD" --ui       || TESTS="FAILED (see above)"
    echo "== Stress test (audio thread + UI)";       "$T" "$BUILD" --stress 15 || TESTS="FAILED (see above)"
    "$T" "$BUILD" --snapshot "$ROOT/SIX-screenshot.png" demo >/dev/null 2>&1 || true
fi

OUT="$BUILD/SIX_artefacts/Release/VST3/SIX.vst3"
codesign --force --deep --sign - "$OUT"

DEST="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$DEST"
rm -rf "$DEST/SIX.vst3"
cp -R "$OUT" "$DEST/"
xattr -dr com.apple.quarantine "$DEST/SIX.vst3" 2>/dev/null || true

echo
echo "Tests:        $TESTS"
echo "Architecture: $(lipo -archs "$DEST/SIX.vst3/Contents/MacOS/SIX")"
echo "Built:        $OUT"
echo "Installed:    $DEST/SIX.vst3"
echo "In Live: Settings > Plug-Ins > enable 'Use VST3 Plug-In System Folders' and press Rescan."
