# PPC / Tiger-Leopard port

This branch (`ppc-tiger`) ports the `v1.x` line of this plugin (Godot 3.2-3.4
GDNative VCS interface) to PowerPC Mac OS X 10.4 (Tiger) / 10.5 (Leopard), for
use with [SamBushman/godot-ports](https://github.com/SamBushman/godot-ports).

## What's different from upstream v1.x

- `SConstruct` gained an `arch=ppc` option (osx only) that switches to a
  single-arch, Tigerbrew `gcc-7`/`g++-7` build instead of the default
  Clang/universal x86_64+arm64 one, and links via Tigerbrew's `ld64`
  (the stock Tiger `/usr/bin/ld` cannot produce a `MH_DYLIB` from a static
  archive containing common symbols - see `-B` flag below).
- `godot-cpp` points at [SamBushman/godot-cpp `ppc-tiger`](https://github.com/SamBushman/godot-cpp/tree/ppc-tiger),
  a one-commit fork of the upstream `3.x` branch adding the same `ppc`
  `macos_arch` option there.
- `demo/addons/godot-git-plugin/git_api.gdnlib` gained an `OSX.32` entry
  (Tiger/Leopard PPC Godot is a 32-bit build; Godot's GDNativeLibrary loader
  matches `OS.<bits>` via `OS::has_feature()`, and `sizeof(void*)==4` reports
  feature `"32"`, not `"64"`).
- SSH/HTTPS remote transports are disabled in libgit2, matching upstream's
  own `build_libs_mac.sh` — this plugin never implements push/pull/fetch/
  clone on any platform (Godot's VCS panel is local-only: status, diff,
  stage, commit, branches), so this costs nothing here.

## Build recipe (native, on the Tiger/Leopard machine itself)

Prerequisites (all via [Tigerbrew](https://github.com/mistydemeo/tigerbrew)):
`gcc-7`, `cmake`, `python3` (+ `pip3 install scons`), `ld64`.

```sh
export PATH="/usr/local/opt/ld64/bin:/usr/local/bin:$PATH"
SDK=/Developer/SDKs/MacOSX10.4u.sdk   # or the 10.5 SDK on Leopard

# 1. libgit2 (vendored source, static lib, no SSH/HTTPS/iconv)
cd godot-git-plugin/thirdparty/libgit2
mkdir build && cd build
cmake .. -DCMAKE_C_COMPILER=gcc-7 \
  -DCMAKE_OSX_SYSROOT=$SDK -DCMAKE_OSX_DEPLOYMENT_TARGET=10.4 \
  -DCMAKE_C_FLAGS="-mmacosx-version-min=10.4" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_CLAR=OFF -DBUILD_EXAMPLES=OFF \
  -DUSE_SSH=OFF -DUSE_HTTPS=OFF -DUSE_BUNDLED_ZLIB=ON -DUSE_ICONV=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build .
cd ../../../..
mkdir -p demo/addons/godot-git-plugin/osx
cp godot-git-plugin/thirdparty/libgit2/build/libgit2.a demo/addons/godot-git-plugin/osx/

# 2. GDNative API json, generated from the actual target editor binary
#    (run this ON the Tiger/Leopard machine against your godot-ports build)
/path/to/godot.osx.opt.tools.ppc --gdnative-generate-json-api api.json

# 3. godot-cpp bindings
cd godot-cpp
python3 -m SCons platform=osx macos_arch=ppc target=release \
  macos_sdk_path=$SDK macos_deployment_target=10.4 \
  generate_bindings=yes custom_api_file=../api.json \
  cc=gcc-7 cxx=g++-7 -j2
cd ..

# 4. the plugin itself
python3 -m SCons platform=osx arch=ppc target=release \
  macos_sdk_path=$SDK cc=gcc-7 cxx=g++-7 -j2
```

Output: `demo/addons/godot-git-plugin/osx/release/libgitapi.dylib` (ppc,
depends on Tigerbrew's `libstdc++.6.dylib`/`libgcc_s.1.dylib` — bundle these
alongside if distributing outside a machine with Tigerbrew installed, the
same way the godot-ports editor `.app` itself does).

## Installing into a project

Copy `demo/addons/godot-git-plugin/` into `res://addons/godot-git-plugin/`
in your project.

**Do not** try to enable it via Project Settings > Plugins — that will fail
with "Base type is not EditorPlugin". That tab is only for addons whose
`plugin.cfg` `script=` points at an `EditorPlugin`; this addon's
`git_api.gdns` is an `EditorVCSInterface`, a different, VCS-specific
extension point with its own discovery mechanism (`editor_node.cpp` hard-
requires `EditorPlugin` for anything toggled from that tab — stock Godot
behavior on every platform, not a port issue). Leave it absent/disabled in
that list.

Instead:

1. Just having the files under `res://addons/godot-git-plugin/` is enough —
   `git_api.gdns` declares `script_class_name = "GitAPI"`, which Godot's
   filesystem scanner auto-registers as a global class as soon as it sees
   the file (no manual `project.godot` editing needed). Reopening the
   project forces a rescan if it doesn't show up immediately.
2. Open **Project menu > Version Control > Set Up Version Control**. Its
   dropdown lists every auto-discovered class whose base is
   `EditorVCSInterface` (see `fetch_available_vcs_plugin_names()` in
   `editor/plugins/version_control_editor_plugin.cpp`) — "GitAPI" should be
   there. Select it and confirm.
3. The Version Control dock and the rest of the Project > Version Control
   submenu (stage, commit, diff, branches) light up after that.

Also note: `EditorVCSInterface`-derived classes can only be instantiated by
the actual editor (`Engine.is_editor_hint()`), not from an exported game or
`-s` script mode — another upstream Godot restriction on every platform,
not specific to this port.

## Verified working (2026-09-12, on a G4 running Tiger 10.4.11)

`_get_vcs_name`, `_initialize`, `_is_vcs_initialized`, `_get_modified_files_data`,
`_stage_file`, `_commit` all exercised live inside the real editor against a
real on-disk git repo; commits created by the plugin were confirmed
independently via `git log` on the same repo.
