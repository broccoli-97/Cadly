#!/usr/bin/env bash
# Install (or verify) Cadly's build dependencies on macOS via Homebrew.
#
# Mirrors the Linux "system packages" strategy: Homebrew's current Qt and
# OCCT rather than an hours-long vcpkg source build. Homebrew Qt is >= 6.8,
# so macOS gets the qlementine style path (like the linux-qt68-* presets).
#
# Usage:
#   scripts/setup-macos.sh            # install anything missing, then verify
#   scripts/setup-macos.sh --check    # verify only, install nothing
set -euo pipefail

CHECK_ONLY=0
[[ "${1:-}" == "--check" ]] && CHECK_ONLY=1

if ! command -v brew >/dev/null 2>&1; then
  echo "error: Homebrew is required (https://brew.sh)" >&2
  exit 1
fi

# qt: Qt 6 (Widgets, OpenGLWidgets, Concurrent, Svg, Test, Linguist tools).
# opencascade: OCCT >= 7.7 (STEP/IGES import); src/cad handles its target names.
FORMULAE=(cmake ninja qt opencascade glm fmt spdlog)

missing=()
for f in "${FORMULAE[@]}"; do
  if ! brew list --formula --versions "$f" >/dev/null 2>&1; then
    missing+=("$f")
  fi
done

if ((${#missing[@]})); then
  if ((CHECK_ONLY)); then
    echo "missing formulae: ${missing[*]}" >&2
    echo "run scripts/setup-macos.sh (without --check) to install them" >&2
    exit 1
  fi
  echo "installing: ${missing[*]}"
  brew install "${missing[@]}"
else
  echo "all formulae already installed"
fi

echo
echo "versions:"
for f in "${FORMULAE[@]}"; do
  brew list --formula --versions "$f"
done

# The macos-* presets put both Homebrew prefixes (/opt/homebrew on Apple
# Silicon, /usr/local on Intel) on CMAKE_PREFIX_PATH; CMake skips whichever
# doesn't exist. Verify the install landed under the active prefix.
BREW_PREFIX="$(brew --prefix)"
for pkg in qt opencascade; do
  if [[ ! -e "$BREW_PREFIX/opt/$pkg" ]]; then
    echo "error: expected $BREW_PREFIX/opt/$pkg to exist" >&2
    exit 1
  fi
done

echo
echo "ready. configure and build with:"
echo "  cmake --preset macos-release"
echo "  cmake --build --preset macos-release"
echo "  ctest  --preset macos-release"
