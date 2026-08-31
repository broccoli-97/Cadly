#!/usr/bin/env bash
# Assemble a self-contained Cadly.app (plus cad_import_cli) from a macos-*
# build tree. macdeployqt copies the Qt frameworks/plugins AND the OCCT/fmt/
# spdlog dylib closure into Contents/Frameworks, rewriting install names to
# @executable_path — the macOS analogue of the Linux script's patchelf work.
# The workflow (or a human) turns the resulting directory into a .dmg.

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <build-directory> <package-directory>" >&2
  exit 2
fi

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
build_dir=$(cd "$1" && pwd)
package_dir="$2"

if [[ ! -d "$build_dir/bin/cadly.app" ||
      ! -x "$build_dir/bin/cad_import_cli" ]]; then
  echo "release binaries not found under $build_dir/bin" >&2
  exit 1
fi

# Homebrew's qt keg links macdeployqt into its bin; prefer PATH, fall back
# to the keg so the script works from a shell without brew shellenv.
macdeployqt=$(command -v macdeployqt || true)
if [[ -z "$macdeployqt" && -x "$(brew --prefix qt 2>/dev/null)/bin/macdeployqt" ]]; then
  macdeployqt="$(brew --prefix qt)/bin/macdeployqt"
fi
if [[ -z "$macdeployqt" ]]; then
  echo "macdeployqt not found (brew install qt)" >&2
  exit 1
fi

mkdir -p "$(dirname "$package_dir")"
package_dir=$(cd "$(dirname "$package_dir")" && pwd)/$(basename "$package_dir")
if [[ -z "$package_dir" || "$package_dir" == "/" ||
      "$package_dir" == "$repo_root" ]]; then
  echo "refusing unsafe package directory: $package_dir" >&2
  exit 1
fi
package_marker="$package_dir/.cadly-macos-package"
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
# Assemble in a private temp dir and move into place at the end. The obvious
# in-place assembly breaks under a synced folder (Dropbox): the sync client
# races macdeployqt and codesign, touching freshly written binaries between a
# tool's write and its verify, which fails signing nondeterministically.
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/cadly-macos-package.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT

app="$work_dir/Cadly.app"
cp -R "$build_dir/bin/cadly.app" "$app"

# Runtime assets go to Contents/Resources, where platform::find_asset_dir's
# bundle candidate (<exe>/../Resources) looks for them.
mkdir -p "$app/Contents/Resources"
cp -R "$repo_root/shaders" "$app/Contents/Resources/shaders"
cp -R "$repo_root/themes" "$app/Contents/Resources/themes"

# Ship the headless import CLI inside the bundle so it shares the deployed
# dylib closure; -executable makes macdeployqt rewrite its load commands too.
cp "$build_dir/bin/cad_import_cli" "$app/Contents/MacOS/"

# Tolerate macdeployqt's exit status: its internal ad-hoc re-sign/verify of
# each patched dylib fails spuriously at times, and this script supersedes it
# — every binary is rewritten, re-signed, and verified below, so a genuine
# deployment failure still fails loudly at those gates.
"$macdeployqt" "$app" \
  -executable="$app/Contents/MacOS/cad_import_cli" \
  -verbose=1 || echo "macdeployqt exited $? (continuing; re-signed below)" >&2

# macdeployqt ships only the cocoa platform plugin. Add offscreen too: it is
# tiny, references Qt purely via @rpath (resolved against the bundled
# frameworks like cocoa's), and lets the packaged app run headless —
# QT_QPA_PLATFORM=offscreen — which CI's smoke step and any scripted use of
# --screenshot depend on.
qt_plugins="$(dirname "$(dirname "$macdeployqt")")/share/qt/plugins"
if [[ ! -f "$qt_plugins/platforms/libqoffscreen.dylib" ]]; then
  qt_plugins="$(brew --prefix qt)/share/qt/plugins"
fi
cp "$qt_plugins/platforms/libqoffscreen.dylib" \
   "$app/Contents/PlugIns/platforms/"

# The OCCT dylibs reference each other as @rpath/libTK*.dylib, and both
# executables still carry the build-time LC_RPATH /opt/homebrew/lib — which
# dyld searches first, quietly loading a SECOND copy of every OCCT toolkit
# from Homebrew next to the bundled set. Two copies means two sets of OCCT's
# global registries and a null-deref inside the CAF document machinery.
# Strip the Homebrew rpaths from every binary and make the executables
# resolve @rpath against the bundled Frameworks instead.
while IFS= read -r -d '' bin; do
  chmod u+w "$bin"
  while IFS= read -r rp; do
    install_name_tool -delete_rpath "$rp" "$bin"
  done < <(otool -l "$bin" \
           | awk '/LC_RPATH/{grab=2;next} grab&&/path /{print $2;grab=0}' \
           | grep -E '^(/opt/homebrew|/usr/local)/' || true)
done < <(find "$app" -type f \( -perm -111 -o -name '*.dylib' \) -print0)
for exe in "$app/Contents/MacOS/cadly" "$app/Contents/MacOS/cad_import_cli"; do
  # Check actual LC_RPATH entries — grepping the whole otool -l output would
  # false-positive on the Qt load commands macdeployqt rewrote to
  # @executable_path/../Frameworks/Qt*.framework/...
  rpaths=$(otool -l "$exe" \
    | awk '/LC_RPATH/{grab=2;next} grab&&/path /{print $2;grab=0}')
  if ! grep -qx '@executable_path/../Frameworks' <<<"$rpaths"; then
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$exe"
  fi
done

# macdeployqt reliably deploys first-order dependencies but can leave a
# copied dylib's own references untouched (observed: libbrotlidec still
# loading libbrotlicommon from /opt/homebrew). Sweep to a fixed point:
# rewrite every remaining Homebrew reference to the bundled copy, pulling in
# any dylib macdeployqt missed entirely.
fw="$app/Contents/Frameworks"
mkdir -p "$fw"
for _pass in 1 2 3 4 5; do
  changed=0
  while IFS= read -r -d '' bin; do
    while IFS= read -r dep; do
      base=$(basename "$dep")
      if [[ ! -e "$fw/$base" ]]; then
        cp "$dep" "$fw/$base"
        chmod u+w "$fw/$base"
        install_name_tool -id "@executable_path/../Frameworks/$base" "$fw/$base"
      fi
      chmod u+w "$bin"
      if [[ "$base" == "$(basename "$bin")" ]]; then
        # A dylib's first otool -L line is its own install name (LC_ID_DYLIB),
        # which -change never rewrites — fix it with -id or the sweep loops.
        install_name_tool -id "@executable_path/../Frameworks/$base" "$bin"
      else
        install_name_tool -change "$dep" \
          "@executable_path/../Frameworks/$base" "$bin"
      fi
      changed=1
    done < <(otool -L "$bin" \
             | awk '/^\t(\/opt\/homebrew|\/usr\/local)\//{print $1}')
  done < <(find "$app" -type f \( -perm -111 -o -name '*.dylib' \) -print0)
  [[ $changed -eq 0 ]] && break
done
if [[ $changed -ne 0 ]]; then
  echo "dylib reference rewrite did not converge" >&2
  exit 1
fi

# macdeployqt's framework surgery invalidates the build-time ad-hoc
# signatures, and current macOS refuses to exec unsigned arm64 binaries.
# Strip Finder/sync-client xattr detritus first (it breaks sealing), then
# re-sign everything ad hoc and verify — distribution signing/notarization
# is a separate, credentialed step outside this script.
xattr -cr "$app"
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"

# Validate self-containment: nothing in the bundle may still resolve against
# the Homebrew prefixes, and the packaged CLI must run in place.
bad=$(find "$app" -type f \( -perm -111 -o -name '*.dylib' \) \
        -exec otool -L {} + 2>/dev/null \
      | grep -E '^\s+(/opt/homebrew|/usr/local)/' || true)
if [[ -n "$bad" ]]; then
  echo "packaged binaries still reference Homebrew libraries:" >&2
  echo "$bad" >&2
  exit 1
fi
"$app/Contents/MacOS/cad_import_cli" "$repo_root/test_files/as1-ug-214.stp" \
  >/dev/null

mkdir -p "$work_dir/sample-files"
cp "$repo_root/test_files/as1-ug-214.stp" "$work_dir/sample-files/"
cp "$repo_root/packaging/macos/README.txt" "$work_dir/"

# Everything validated — move the finished package into place in one step.
mkdir -p "$package_dir"
touch "$package_marker"
mv "$app" "$work_dir/sample-files" "$work_dir/README.txt" "$package_dir/"

echo "packaged: $package_dir/Cadly.app"
