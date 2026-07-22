#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <build-directory> <package-directory>" >&2
  exit 2
fi

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
build_dir=$(realpath "$1")
package_dir=$(realpath -m "$2")

if [[ ! -d "$build_dir" || ! -x "$build_dir/bin/cadly" ||
      ! -x "$build_dir/bin/cad_import_cli" ]]; then
  echo "release binaries not found under $build_dir/bin" >&2
  exit 1
fi
if [[ -z "$package_dir" || "$package_dir" == "/" ||
      "$package_dir" == "$repo_root" ]]; then
  echo "refusing unsafe package directory: $package_dir" >&2
  exit 1
fi
for tool in cmake ldd patchelf qmake6 strip; do
  if ! command -v "$tool" >/dev/null; then
    echo "required packaging tool not found: $tool" >&2
    exit 1
  fi
done

package_marker="$package_dir/.cadly-portable-package"
if [[ -d "$package_dir" ]]; then
  if [[ ! -f "$package_marker" ]] &&
      [[ -n "$(find "$package_dir" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
    echo "refusing to replace unmarked non-empty directory: $package_dir" >&2
    exit 1
  fi
  rm -rf "$package_dir"
elif [[ -e "$package_dir" ]]; then
  echo "package path exists and is not a directory: $package_dir" >&2
  exit 1
fi
mkdir -p "$package_dir"
touch "$package_marker"
cmake --install "$build_dir" --prefix "$package_dir"

# FetchContent dependencies install development metadata. Replace that lib/
# tree with the runtime dependency closure assembled below.
rm -rf "$package_dir/include" "$package_dir/lib"
mkdir -p "$package_dir/lib" "$package_dir/plugins" \
  "$package_dir/sample-files"
cp "$repo_root/test_files/as1-ug-214.stp" "$package_dir/sample-files/"
cp "$repo_root/packaging/linux/README.txt" "$package_dir/"
cp "$repo_root/packaging/linux/qt.conf" "$package_dir/bin/"

qt_plugins=$(qmake6 -query QT_INSTALL_PLUGINS)
if [[ ! -d "$qt_plugins" ]]; then
  echo "Qt plugin directory not found: $qt_plugins" >&2
  exit 1
fi

copy_plugin() {
  local relative_path=$1
  local source_path="$qt_plugins/$relative_path"
  if [[ ! -f "$source_path" ]]; then
    echo "required Qt plugin not found: $source_path" >&2
    exit 1
  fi
  mkdir -p "$package_dir/plugins/$(dirname "$relative_path")"
  cp -L "$source_path" "$package_dir/plugins/$relative_path"
}

copy_plugin_dir() {
  local relative_dir=$1
  local source_dir="$qt_plugins/$relative_dir"
  if [[ ! -d "$source_dir" ]]; then
    echo "required Qt plugin directory not found: $source_dir" >&2
    exit 1
  fi
  mkdir -p "$package_dir/plugins/$relative_dir"
  find "$source_dir" -maxdepth 1 -type f -name '*.so' \
    -exec cp -L -t "$package_dir/plugins/$relative_dir" {} +
}

# X11/XWayland, native Wayland, CI's headless validation platform, and the
# SVG handlers used by the qlementine icon theme.
copy_plugin platforms/libqxcb.so
copy_plugin platforms/libqoffscreen.so
copy_plugin platforms/libqwayland-egl.so
copy_plugin platforms/libqwayland-generic.so
copy_plugin_dir xcbglintegrations
copy_plugin_dir wayland-decoration-client
copy_plugin_dir wayland-graphics-integration-client
copy_plugin_dir wayland-shell-integration
copy_plugin_dir platforminputcontexts
copy_plugin_dir imageformats
copy_plugin_dir iconengines

is_system_runtime() {
  case "$1" in
    ld-linux-*.so.*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|\
    librt.so.*|libresolv.so.*|libutil.so.*|libnss_*.so.*)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

mapfile -d '' runtime_roots < <(
  printf '%s\0' "$package_dir/bin/cadly" "$package_dir/bin/cad_import_cli"
  find "$package_dir/plugins" -type f -name '*.so' -print0
)

# ldd emits the complete transitive closure for each executable/plugin. Copy
# every non-glibc object; graphics-driver libraries loaded later by GLVND stay
# on the host, where they must match the installed kernel/GPU driver.
for object in "${runtime_roots[@]}"; do
  ldd_output=$(ldd "$object")
  if grep -q '=> not found' <<<"$ldd_output"; then
    echo "unresolved dependencies for $object:" >&2
    grep '=> not found' <<<"$ldd_output" >&2
    exit 1
  fi

  while IFS= read -r dependency; do
    soname=$(basename "$dependency")
    if is_system_runtime "$soname"; then
      continue
    fi
    destination="$package_dir/lib/$soname"
    if [[ ! -e "$destination" ]]; then
      cp -L "$dependency" "$destination"
    elif ! cmp -s "$dependency" "$destination"; then
      echo "dependency basename collision for $soname" >&2
      echo "  existing: $destination" >&2
      echo "  second:   $dependency" >&2
      exit 1
    fi
  done < <(
    awk '/=> \// { print $3 } /^[[:space:]]*\// { print $1 }' \
      <<<"$ldd_output" | sort -u
  )
done

# Keep every lookup inside the extracted package without requiring a launcher
# script or LD_LIBRARY_PATH. Plugins are one directory deeper than binaries.
patchelf --set-rpath '$ORIGIN/../lib' \
  "$package_dir/bin/cadly" "$package_dir/bin/cad_import_cli"
while IFS= read -r -d '' library; do
  patchelf --set-rpath '$ORIGIN' "$library"
done < <(find "$package_dir/lib" -type f -name '*.so*' -print0)
while IFS= read -r -d '' plugin; do
  patchelf --set-rpath '$ORIGIN/../../lib' "$plugin"
done < <(find "$package_dir/plugins" -type f -name '*.so' -print0)

# RelWithDebInfo is useful in the build tree, but shipped debug sections only
# inflate downloads. Windows packaging likewise omits PDBs.
strip --strip-unneeded "$package_dir/bin/cadly" \
  "$package_dir/bin/cad_import_cli"
find "$package_dir/lib" "$package_dir/plugins" -type f -name '*.so*' \
  -exec strip --strip-unneeded {} +

# Verify direct NEEDED entries rather than trusting the CI host's installed
# libraries to mask an incomplete bundle. Only glibc-family ABI libraries may
# resolve outside package/lib.
mapfile -d '' packaged_elfs < <(
  printf '%s\0' "$package_dir/bin/cadly" "$package_dir/bin/cad_import_cli"
  find "$package_dir/lib" "$package_dir/plugins" -type f -name '*.so*' -print0
)
for object in "${packaged_elfs[@]}"; do
  while IFS= read -r needed; do
    if [[ -e "$package_dir/lib/$needed" ]] || is_system_runtime "$needed"; then
      continue
    fi
    echo "portable package is missing $needed (required by $object)" >&2
    exit 1
  done < <(patchelf --print-needed "$object")
done

echo "Portable Linux package assembled at $package_dir"
du -sh "$package_dir"
