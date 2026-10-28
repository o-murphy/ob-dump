#!/usr/bin/env bash
# Tests or builds example/ (a Flutter app that depends on the local ob_dump_reader by
# path and reads an LMDB store) for one target, then checks that the library
# the build hook compiled actually ended up in the build output.
#
#   scripts/ci/flutter-smoke.sh [test|linux|macos|ios|windows|android]
#
# `test` runs `flutter test` (the case that used to need a hand-built library
# and a package: URI lookup); the others run the release `flutter build` of that
# target, which is what ebalistyka does. The app must use the library: code
# assets are tree-shaken, so an unreferenced library would not be bundled.
set -euo pipefail

TARGET="${1:-test}"
cd "$(dirname "${BASH_SOURCE[0]}")/../../dart/example"

flutter pub get

# Runs the built example in self-test mode (it loads the native LMDB library, reads a store,
# writes the result to a file and exits 0, or exits 1 with the reason). With
# `fail`, the caller removed the library first, so open() has to fail loudly.
# Only in CI: on Linux it opens a window, which needs a display (xvfb).
selftest() { # selftest <ok|fail> <exe>
  local want="$1" exe="$2" out code=0 runner=() pid wd log
  [ -n "${CI:-}" ] || { echo "skip: self-test run (set CI=1 to run the built app)"; return 0; }
  out="$(mktemp -u)"; log="$(mktemp)"
  if [ "$(uname -s)" = Linux ] && command -v xvfb-run >/dev/null; then runner=(xvfb-run -a); fi
  # Own watchdog instead of timeout(1): macOS has none, and a GUI app that never
  # reaches exit() would otherwise hold the runner until the job times out.
  OB_DUMP_EXAMPLE_SELFTEST=1 OB_DUMP_EXAMPLE_SELFTEST_OUT="$out" \
    ${runner[@]+"${runner[@]}"} "$exe" >"$log" 2>&1 &
  pid=$!
  ( sleep 120; pkill -9 -P "$pid" 2>/dev/null; kill -9 "$pid" 2>/dev/null ) >/dev/null 2>&1 &
  wd=$!
  wait "$pid" || code=$?
  kill "$wd" 2>/dev/null || true
  [ "$code" -ne 137 ] || echo "the self-test did not finish within 120 s and was killed" >&2
  local msg; msg="$(cat "$out" 2>/dev/null || true)"
  # No result file (the app could not write it): the same text is on stdout.
  [ -n "$msg" ] || msg="$(cat "$log" 2>/dev/null || true)"
  # The app's own stdout/stderr, so a hang or crash is not silent.
  [ "$code" -eq 0 ] || { echo "--- app output (exit $code) ---" >&2; cat "$log" >&2 || true; echo "--- end ---" >&2; }
  if [ "$want" = ok ]; then
    if [ "$code" -eq 0 ] && [[ "$msg" == *"ob_dump_reader self-test ok"* ]]; then echo "OK: the built app loaded liblmdb and read a store"
    else echo "FAILED: the built app could not run liblmdb (exit $code): $msg" >&2; exit 1; fi
  else
    if [ "$code" -ne 0 ] && [[ "$msg" == *"Could not load the native LMDB library"* ]]; then echo "OK: with the library removed the app fails fast: ${msg%%$'\n'*}"
    else echo "FAILED: the app did not fail fast without the library (exit $code): $msg" >&2; exit 1; fi
  fi
}

have() { # have <description> <find-expression...>
  local what="$1"; shift
  if [ -n "$("$@" 2>/dev/null | head -1)" ]; then echo "OK: $what"; else echo "MISSING: $what" >&2; exit 1; fi
}

case "$TARGET" in
  test)
    flutter test
    ;;
  linux)
    flutter build linux --release
    have "liblmdb.so in the bundle" find build/linux -name 'liblmdb.so'
    BUNDLE="$(dirname "$(find build/linux -type f -name ob_dump_example | head -1)")"
    selftest ok "$BUNDLE/ob_dump_example"
    rm -f "$BUNDLE"/lib/liblmdb.so
    selftest fail "$BUNDLE/ob_dump_example"
    ;;
  macos)
    # example/ has no macos/ runner checked in; generate it (keeps lib/ and
    # pubspec.yaml as they are).
    [ -d macos ] || flutter create --platforms=macos --project-name ob_dump_example .
    # LMDB synchronises processes with named POSIX semaphores on macOS
    # (sem_open), which the App Sandbox forbids (mdb_env_open: "Operation not
    # permitted"), so the smoke app runs unsandboxed -- the same limitation
    # applies to any sandboxed (e.g. Mac App Store) app using this package.
    perl -0pi -e 's#(com\.apple\.security\.app-sandbox</key>\s*)<true/>#$1<false/>#' macos/Runner/*.entitlements
    grep -A1 'app-sandbox' macos/Runner/Release.entitlements | grep -q '<false/>' \
      || { echo "could not disable the App Sandbox in macos/Runner/Release.entitlements" >&2; exit 1; }
    flutter pub get
    flutter build macos --release
    APP="$(find build/macos -type d -name 'ob_dump_example.app' | head -1)"
    have "lmdb library inside the .app" find "$APP" -name '*lmdb*'
    selftest ok "$APP/Contents/MacOS/ob_dump_example"
    find "$APP" -name '*lmdb*' -exec rm -rf {} +
    selftest fail "$APP/Contents/MacOS/ob_dump_example"
    ;;
  ios)
    # Like macos: the runner is generated, not checked in.
    [ -d ios ] || flutter create --platforms=ios --project-name ob_dump_example .
    flutter pub get
    # 1. Device build (arm64), unsigned: the hook has to compile for iphoneos
    #    and Flutter has to put the library into Runner.app/Frameworks.
    flutter build ios --release --no-codesign
    APP="$(find build/ios -type d -name 'Runner.app' | head -1)"
    have "lmdb library inside Runner.app" find "$APP" -name '*lmdb*'
    LIB="$(find "$APP" -type f -name '*lmdb*' | head -1)"
    ARCHS="$(lipo -archs "$LIB")"
    case "$ARCHS" in *arm64*) echo "OK: device library is $ARCHS" ;;
      *) echo "MISSING: arm64 in $LIB (found: $ARCHS)" >&2; exit 1 ;; esac
    # 2. Load and compute on a simulator.
    UDID="$(xcrun simctl list devices available | grep -m1 'iPhone' | grep -oE '[0-9A-F]{8}(-[0-9A-F]{4}){3}-[0-9A-F]{12}' || true)"
    [ -n "$UDID" ] || { echo "no iPhone simulator available on this runner" >&2; exit 1; }
    xcrun simctl boot "$UDID" || true
    xcrun simctl bootstatus "$UDID" -b
    # Bounded: a hang here (e.g. the app crashing at launch while flutter waits
    # for its VM service) must fail the job with the simulator log, not run
    # until the job timeout.
    set +e
    ( sleep 900; echo "iOS integration test exceeded 15 min" >&2; pkill -f 'flutter_tools' ) &
    WD=$!
    flutter test integration_test -d "$UDID" --reporter expanded
    CODE=$?
    kill "$WD" 2>/dev/null
    set -e
    if [ "$CODE" -ne 0 ]; then
      echo "--- simulator log for Runner (last 10 min) ---" >&2
      xcrun simctl spawn "$UDID" log show --last 10m --style compact \
        --predicate 'process == "Runner" OR eventMessage CONTAINS[c] "lmdb" OR eventMessage CONTAINS[c] "ob_dump"' 2>&1 | tail -150 >&2 || true
      echo "--- end ---" >&2
      exit "$CODE"
    fi
    ;;
  windows)
    flutter build windows --release
    have "lmdb.dll next to the exe" find build/windows -name 'lmdb.dll'
    EXE="$(find build/windows -type f -name ob_dump_example.exe | head -1)"
    selftest ok "$EXE"
    rm -f "$(dirname "$EXE")/lmdb.dll"
    selftest fail "$EXE"
    ;;
  android)
    flutter build apk --release
    APK="$(find build/app/outputs -name '*.apk' | head -1)"
    for abi in arm64-v8a armeabi-v7a x86_64; do
      have "lib/$abi/liblmdb.so in the APK" unzip -l "$APK" "lib/$abi/liblmdb.so"
    done
    # Google Play: 64-bit libraries must be 16 KB page aligned (every LOAD
    # segment), or the app is rejected / fails on 16 KB devices.
    SO="$(mktemp -d)"
    for abi in arm64-v8a x86_64; do
      unzip -p "$APK" "lib/$abi/liblmdb.so" > "$SO/$abi.so"
      # Every DT_NEEDED must be a system library: nothing else is in the APK.
      if readelf -d "$SO/$abi.so" | grep -q 'libc++_shared'; then
        echo "MISSING: $abi liblmdb.so needs libc++_shared.so, which is not bundled" >&2; exit 1
      fi
      for align in $(readelf -lW "$SO/$abi.so" | awk '$1=="LOAD" {print $NF}'); do
        [ $((align)) -ge 16384 ] || { echo "MISSING: $abi LOAD segment aligned to $align, need >= 0x4000 (16 KB)" >&2; exit 1; }
      done
      echo "OK: $abi liblmdb.so is 16 KB page aligned"
    done
    ;;
  *) echo "unknown target: $TARGET" >&2; exit 2 ;;
esac
