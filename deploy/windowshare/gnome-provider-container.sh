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
  libei-dev libeis-dev gcc pkg-config dbus-daemon dbus-bin libglib2.0-bin \
  libgl1-mesa-dri libegl-mesa0 fonts-dejavu-core nodejs
dbus-uuidgen --ensure=/etc/machine-id

# This account exists only in this container; never adjust the checkout's owner.
fixture_user=deskflow-gnome-fixture
if ! id "$fixture_user" >/dev/null 2>&1; then
  useradd --create-home --shell /bin/sh "$fixture_user"
fi
scratch=$(mktemp -d /tmp/deskflow-gnome-container-XXXXXX)
trap 'rm -rf -- "$scratch"' EXIT
chown "$fixture_user:$fixture_user" "$scratch"
mkdir -p "$(dirname "$output_path")"

node --test "$source_root/deploy/windowshare/gnome-provider/tests/logic.test.js"
set +e
runuser -u "$fixture_user" -- /usr/bin/python3 \
  "$source_root/deploy/windowshare/gnome-provider-tests.py" --output "$scratch/report"
result=$?
set -e
if [[ -d "$scratch/report" ]]; then
  chmod -R a+rX "$scratch/report"
  cp -a "$scratch/report" "$output_path"
fi
exit "$result"
