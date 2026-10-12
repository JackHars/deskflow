#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
set -euo pipefail

# Run only inside a disposable Ubuntu 26.04 Docker container. No privileged
# mode, host networking, display sockets, /dev/dri or /dev/input are required.
# Example (from the checkout, with an already-running local Docker daemon):
#   docker run --rm --init -v "$PWD:/source:ro" -v "$PWD/build:/results" \
#     ubuntu:26.04 bash /source/deploy/windowshare/gnome-provider-container.sh \
#     /source /results/gnome-provider
# The output directory must not exist. It receives logs even when the real
# compositor test fails; a source-only or mocked result never counts as PASS.

if [[ $# != 2 || $(id -u) != 0 || ! -f /.dockerenv ]]; then
  echo "Usage: run as root in a disposable Ubuntu 26.04 Docker container: $0 CHECKOUT NEW_OUTPUT" >&2
  exit 2
fi
source_root=$(realpath "$1")
output_path=$(realpath -m "$2")
if [[ -e "$output_path" || ! -f "$source_root/deploy/windowshare/gnome-provider-tests.py" ]]; then
  echo "Output must be new and CHECKOUT must contain the harness" >&2
  exit 2
fi
. /etc/os-release
if [[ ${ID:-} != ubuntu || ${VERSION_ID:-} != 26.04 ]]; then
  echo "This reproducible fixture requires Ubuntu 26.04" >&2
  exit 2
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
  gnome-shell mutter gjs python3 python3-gi gir1.2-gtk-4.0 \
  libei-dev libeis-dev build-essential pkg-config dbus-daemon dbus-bin libglib2.0-bin libglib2.0-dev \
  libgl1-mesa-dri libegl-mesa0 fonts-dejavu-core nodejs \
  cmake ninja-build qt6-base-dev qt6-base-private-dev qt6-tools-dev \
  qt6-wayland qt6-wayland-dev qt6-wayland-private-dev libwayland-dev \
  libssl-dev openssl libavcodec-dev libavutil-dev libswscale-dev libx264-dev ffmpeg \
  libxcb-randr0-dev libxcb-xinput-dev libxcb-cursor0 libxkbcommon-x11-0 xvfb xauth
dbus-uuidgen --ensure=/etc/machine-id

# This account exists only in this container; never adjust the checkout's owner.
fixture_user=deskflow-gnome-fixture
if ! id "$fixture_user" >/dev/null 2>&1; then
  useradd --create-home --shell /bin/sh "$fixture_user"
fi
scratch=$(mktemp -d /tmp/deskflow-gnome-container-XXXXXX)
finish() {
  local exit_status=$?
  mkdir -p "$scratch/report"
  for name in readiness configure build application-tests launcher-native; do
    if [[ -f "$scratch/$name.log" ]]; then
      cp "$scratch/$name.log" "$scratch/report/"
      if [[ $exit_status != 0 ]]; then
        tail -n 50 "$scratch/$name.log" >&2
      fi
    fi
  done
  chmod -R a+rX "$scratch/report"
  cp -a "$scratch/report" "$output_path"
  rm -rf -- "$scratch"
}
trap finish EXIT
chown "$fixture_user:$fixture_user" "$scratch"
mkdir -p "$(dirname "$output_path")"

node --test "$source_root/deploy/windowshare/gnome-provider/tests/logic.test.js"
runuser -u "$fixture_user" -- /usr/bin/python3 \
  "$source_root/deploy/windowshare/gnome-provider-readiness-tests.py" \
  >"$scratch/readiness.log" 2>&1
runuser -u "$fixture_user" -- cmake -S "$source_root/src/apps/windowshare" -B "$scratch/build" -G Ninja \
  -DWINDOWSHARE_APP=ON -DWINDOWSHARE_GNOME_FIXTURE=ON -DCMAKE_BUILD_TYPE=Debug \
  >"$scratch/configure.log" 2>&1
runuser -u "$fixture_user" -- cmake --build "$scratch/build" --parallel 2 \
  >"$scratch/build.log" 2>&1
set +e
runuser -u "$fixture_user" -- env QT_QPA_PLATFORM=offscreen LANG=C.UTF-8 \
  ctest --test-dir "$scratch/build" --output-on-failure --timeout 120 \
  >"$scratch/application-tests.log" 2>&1
result=$?
if [[ $result == 0 ]]; then
  runuser -u "$fixture_user" -- /usr/bin/python3 \
    "$source_root/deploy/windowshare/gnome-provider/tests/launcher-native.py" \
    >"$scratch/launcher-native.log" 2>&1
  result=$?
fi
if [[ $result == 0 ]]; then
  runuser -u "$fixture_user" -- /usr/bin/python3 \
  "$source_root/deploy/windowshare/gnome-provider-tests.py" --output "$scratch/report"
  result=$?
fi
if [[ $result == 0 ]]; then
  for scale in 1 1.25; do
    runuser -u "$fixture_user" -- /usr/bin/python3 \
      "$source_root/deploy/windowshare/gnome-qt-viewer-tests.py" \
      --viewer "$scratch/build/deskflow-windowshare" \
      --fixture "$scratch/build/windowshare-gnome-viewer-fixture" \
      --scale "$scale" --output "$scratch/report/qt-scale-$scale"
    result=$?
    if [[ $result != 0 ]]; then
      break
    fi
  done
fi
set -e
exit "$result"
