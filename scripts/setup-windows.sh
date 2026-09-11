#!/usr/bin/env bash
# Run from an up-to-date MSYS2 UCRT64 terminal. All dependencies are binaries
# from the same repository, with a matching compiler and C++ runtime.
set -euo pipefail

if [[ "${MSYSTEM:-}" != UCRT64 ]]; then
  echo "Run this script from an MSYS2 UCRT64 terminal (https://www.msys2.org)." >&2
  exit 1
fi

pacman --sync --needed --noconfirm \
  git \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-qt6-base \
  mingw-w64-ucrt-x86_64-qt6-svg \
  mingw-w64-ucrt-x86_64-qt6-tools \
  mingw-w64-ucrt-x86_64-opencascade \
  mingw-w64-ucrt-x86_64-glm \
  mingw-w64-ucrt-x86_64-fmt \
  mingw-w64-ucrt-x86_64-spdlog
