#!/bin/sh
# Executed with administrator authorization by the final bridge.
# Source paths arrive as positional arguments; no shell eval is used.
set -eu
mode=$1
source_dir=$2
hal=/Library/Audio/Plug-Ins/HAL
support='/Library/Application Support/KORE1'
bundle="$hal/KoreAudio.driver"
if [ "$mode" = install ]; then
    test -f "$source_dir/KoreAudio.driver/Contents/MacOS/KoreAudio"
    test -x "$source_dir/kore1-final-bridge"
    /usr/bin/codesign --verify --deep --strict "$source_dir/KoreAudio.driver"
    test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$source_dir/KoreAudio.driver/Contents/Info.plist")" = org.nikore.experimental.driver
    /bin/mkdir -p "$hal" "$support/Backups"
    stamp=$(/bin/date +%Y%m%d-%H%M%S)-$$
    stage="$hal/.KoreAudio-stage-$stamp.driver"
    /usr/bin/ditto "$source_dir/KoreAudio.driver" "$stage"
    /usr/sbin/chown -R root:wheel "$stage"
    /bin/chmod -R go-w "$stage"
    /usr/bin/codesign --verify --deep --strict "$stage"
    if [ -e "$bundle" ]; then /bin/mv "$bundle" "$support/Backups/KoreAudio-$stamp.driver"; fi
    /bin/mv "$stage" "$bundle"
    if [ -e "$support/kore1-final-bridge" ]; then /bin/cp -p "$support/kore1-final-bridge" "$support/Backups/bridge-$stamp"; fi
    /usr/bin/install -o root -g wheel -m 755 "$source_dir/kore1-final-bridge" "$support/kore1-final-bridge"
    /usr/bin/ditto "$source_dir/KoreAudio.driver" "$support/KoreAudio.driver"
    /usr/sbin/chown -R root:wheel "$support"
    /bin/chmod -R go-w "$support"
elif [ "$mode" = uninstall ]; then
    # Preserve the installed bundle and binary as a reversible backup.
    if [ -e "$bundle" ]; then
        /bin/mkdir -p "$support/Backups"
        /bin/mv "$bundle" "$support/Backups/removed-$(/bin/date +%Y%m%d-%H%M%S)-$$.driver"
    fi
else
    exit 2
fi
# launchd restarts the system audio service. Audio defaults are never changed.
/usr/bin/killall coreaudiod || true
