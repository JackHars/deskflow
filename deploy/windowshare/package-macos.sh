#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
set -euo pipefail

if [ "$#" -lt 2 ] || [ "$#" -gt 4 ]; then
  echo "Usage: $0 APP_BUILD_DIR NEW_OUTPUT_DIR [SIGNING_IDENTITY] [CORE_EXECUTABLE]" >&2
  exit 2
fi
build_dir="$1"
output_dir="$2"
# macdeployqt changes directories internally; extra executables must be absolute.
case "$build_dir" in /*) ;; *) build_dir="$PWD/$build_dir" ;; esac
case "$output_dir" in /*) ;; *) output_dir="$PWD/$output_dir" ;; esac
signing_identity="${3:--}"
core_executable="${4:-}"
if [ -n "$core_executable" ]; then
  case "$core_executable" in /*) ;; *) core_executable="$PWD/$core_executable" ;; esac
  if [ ! -f "$core_executable" ] || [ ! -x "$core_executable" ]; then
    echo "The opt-in Deskflow core executable is missing." >&2
    exit 1
  fi
fi
deploy_tool="${MACDEPLOYQT:-macdeployqt}"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python_tool="${PYTHON3:-python3}"
source_app="$build_dir/deskflow-windowshare.app"
if [ ! -d "$source_app" ]; then
  echo "Build src/apps/windowshare with WINDOWSHARE_APP=ON first." >&2
  exit 1
fi
if [ -e "$output_dir" ]; then
  echo "Refusing to overwrite an existing output directory." >&2
  exit 1
fi
if ! command -v "$deploy_tool" >/dev/null 2>&1; then
  echo "Set MACDEPLOYQT to the deployment tool from the Qt used to build this app." >&2
  exit 1
fi
if ! command -v "$python_tool" >/dev/null 2>&1; then
  echo "Python 3 is required to validate the complete bundle dependency graph." >&2
  exit 1
fi
# Search only the Qt used by macdeployqt and explicit Homebrew library roots.
# Resolving the tool's symlink also handles Homebrew's /bin/macdeployqt shim.
deploy_path="$(command -v "$deploy_tool")"
qt_libraries="$("$python_tool" -c 'from pathlib import Path; import sys; print(Path(sys.argv[1]).resolve().parent.parent / "lib")' "$deploy_path")"
library_roots=(--search-root "$qt_libraries")
mkdir -p "$output_dir"
bundle_name="DeskflowWindowShare-Experimental"
if [ -n "$core_executable" ]; then
  bundle_name="DeskflowSeamless-Core-Experimental"
fi
app_path="$output_dir/$bundle_name.app"
ditto "$source_app" "$app_path"
helper_path="$app_path/Contents/MacOS/deskflow-holding-display-helper"
if [ ! -x "$helper_path" ]; then
  echo "The holding-display helper is missing from the built application bundle." >&2
  exit 1
fi
deploy_options=(-always-overwrite "-executable=$helper_path")
if [ -n "$core_executable" ]; then
  # The running bundle main anchors its resource seal. A standalone core CLI
  # therefore needs its own bundle with core as CFBundleExecutable; merely
  # dropping an extra core into the WindowShare bundle is not a trust anchor.
  ditto "$core_executable" "$app_path/Contents/MacOS/deskflow-core"
  "$python_tool" - "$app_path/Contents/Info.plist" <<'PY'
import plistlib
import sys
from pathlib import Path
path = Path(sys.argv[1])
with path.open("rb") as stream:
    info = plistlib.load(stream)
info.update(CFBundleExecutable="deskflow-core",
            CFBundleIdentifier="org.deskflow.seamless.core.experimental",
            CFBundleName="Deskflow Seamless Core Experimental")
with path.open("wb") as stream:
    plistlib.dump(info, stream)
PY
  deploy_options+=("-executable=$app_path/Contents/MacOS/deskflow-windowshare")
fi
if command -v brew >/dev/null 2>&1; then
  brew_prefix="$(brew --prefix)"
  deploy_options+=("-libpath=$brew_prefix/lib")
  library_roots+=(--search-root "$brew_prefix/lib")
  # Keg-only dependencies may have no symlink in Homebrew's top-level lib.
  for formula_libraries in "$brew_prefix"/opt/*/lib; do
    if [ -d "$formula_libraries" ]; then
      library_roots+=(--search-root "$formula_libraries")
    fi
  done
fi
"$deploy_tool" "$app_path" "${deploy_options[@]}"
# Keep noninteractive CLI/transport diagnostics usable without activating the
# user's desktop. macdeployqt normally copies only the Cocoa platform plugin.
offscreen_plugin="${qt_libraries%/lib}/plugins/platforms/libqoffscreen.dylib"
if [ ! -f "$offscreen_plugin" ]; then
  echo "The selected Qt has no offscreen plugin for packaged diagnostics." >&2
  exit 1
fi
mkdir -p "$app_path/Contents/PlugIns/platforms"
ditto "$offscreen_plugin" "$app_path/Contents/PlugIns/platforms/libqoffscreen.dylib"
# macdeployqt can leave transitive @rpath dependencies uncopied. Close the full
# graph and record the actual oldest supported macOS before any signing.
"$python_tool" "$script_dir/macos-bundle.py" repair "$app_path" "${library_roots[@]}"
# Sign nested libraries, frameworks and the owned helper before sealing the app.
# macdeployqt does not reliably sign additional executables in Contents/MacOS.
while IFS= read -r -d '' library; do
  codesign --force --sign "$signing_identity" "$library"
done < <(find "$app_path/Contents" -type f -name '*.dylib' -print0)
while IFS= read -r -d '' framework; do
  codesign --force --sign "$signing_identity" "$framework"
done < <(find "$app_path/Contents" -depth -type d -name '*.framework' -print0)
codesign --force --sign "$signing_identity" "$helper_path"
if [ -n "$core_executable" ]; then
  codesign --force --sign "$signing_identity" "$app_path/Contents/MacOS/deskflow-windowshare"
fi
codesign --force --sign "$signing_identity" "$app_path"
"$python_tool" "$script_dir/macos-bundle.py" verify "$app_path"
ditto -c -k --sequesterRsrc --keepParent "$app_path" "$output_dir/$bundle_name.zip"
shasum -a 256 "$output_dir/$bundle_name.zip"
echo "Packaged a separate experimental application; the installed Deskflow was not changed."
