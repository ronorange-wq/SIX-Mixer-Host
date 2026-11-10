#!/bin/bash
# Double-click to install SIX.vst3 for the current user (no admin password needed).
cd "$(dirname "$0")"
DEST="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$DEST"
rm -rf "$DEST/SIX.vst3"
cp -R "SIX.vst3" "$DEST/"
xattr -cr "$DEST/SIX.vst3" 2>/dev/null; codesign --force --deep --sign - "$DEST/SIX.vst3" 2>/dev/null
echo "SIX installed: $DEST/SIX.vst3"
echo "In Ableton Live: Settings > Plug-Ins > Rescan."
read -n 1 -s -r -p "Press any key to close"
