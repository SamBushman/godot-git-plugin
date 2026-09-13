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
- SSH/HTTPS remote transports (push/pull/fetch/remote management) are
  **enabled**, backported from the `v2.x` plugin line (which added them;
  upstream `v1.x` never had them). Uses Tigerbrew's `openssl3` and
  `libssh2` packages — both already have real prebuilt PPC Tiger bottles
  (or build from source cleanly; `openssl3` did on this machine, no
  altivec bottle existed for the exact version). See the "Remote
  operations" section below for the exact build flags and a real gotcha
  found along the way (`htonll` doesn't exist on Tiger).
- `_push`/`_pull`/`_fetch`/`_create_remote`/`_remove_remote`/`_get_remotes`/
  `_set_credentials` were written fresh in this codebase's existing plain
  raw-pointer style (not copied verbatim from `v2.x`, which uses a much
  more elaborate RAII-wrapper architecture) — same libgit2 API calls,
  adapted to match the rest of this file. One real upstream bug was
  caught and fixed while doing this: `v2.x`'s `push_update_reference_cb`
  has the success/rejection check backwards (per libgit2's own doc
  comment, `status` is non-NULL only on *rejection* — `v2.x` treats
  non-NULL as success).

## Build recipe (native, on the Tiger/Leopard machine itself)

Prerequisites (all via [Tigerbrew](https://github.com/mistydemeo/tigerbrew)):
`gcc-7`, `cmake`, `python3` (+ `pip3 install scons`), `ld64`.

```sh
export PATH="/usr/local/opt/ld64/bin:/usr/local/bin:$PATH"
SDK=/Developer/SDKs/MacOSX10.4u.sdk   # or the 10.5 SDK on Leopard

# 1. libgit2 (vendored source, static lib, SSH via libssh2 + HTTPS via OpenSSL)
#    Both openssl3 and libssh2 come from Tigerbrew; both are keg-only so
#    their pkgconfig/lib dirs aren't on the default search path.
brew install openssl3 libssh2   # already present if Tigerbrew's own git is installed
export PKG_CONFIG_PATH="/usr/local/opt/openssl3/lib/pkgconfig:/usr/local/opt/libssh2/lib/pkgconfig:/usr/local/opt/zlib/lib/pkgconfig"

cd godot-git-plugin/thirdparty/libgit2
mkdir build && cd build
cmake .. -DCMAKE_C_COMPILER=gcc-7 \
  -DCMAKE_OSX_SYSROOT=$SDK -DCMAKE_OSX_DEPLOYMENT_TARGET=10.4 \
  -DCMAKE_C_FLAGS="-mmacosx-version-min=10.4" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_CLAR=OFF -DBUILD_EXAMPLES=OFF \
  -DUSE_SSH=ON -DUSE_HTTPS=OpenSSL \
  -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl3 \
  -DCMAKE_PREFIX_PATH="/usr/local/opt/openssl3;/usr/local/opt/libssh2" \
  -DUSE_NTLMCLIENT=OFF \
  -DUSE_BUNDLED_ZLIB=ON -DUSE_ICONV=OFF \
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
depends on Tigerbrew's `libstdc++.6.dylib`/`libgcc_s.1.dylib`/`libssh2.1.dylib`/
`libssl.3.dylib`/`libcrypto.3.dylib` — bundle these alongside if distributing
outside a machine with Tigerbrew installed, the same way the godot-ports
editor `.app` itself does).

## Remote operations (SSH/HTTPS) — gotchas found while building this

- **`CMAKE_PREFIX_PATH` is required, not just `PKG_CONFIG_PATH`.**
  `libssh2.pc`'s `Requires.private` on `libssl`/`libcrypto` isn't resolved
  through pkg-config alone by libgit2's own `FIND_PKGLIBRARIES` CMake
  macro — without `CMAKE_PREFIX_PATH` pointing at both kegs, it silently
  resolves to Tiger's ancient stock `/usr/lib/libssl.dylib` (0.9.7l,
  pre-TLS-1.2) instead of Tigerbrew's. Confirmed both ways by inspecting
  CMake's own "Resolved libraries:" configure-log line.
- **Same silent-wrong-library trap for `find_package(OpenSSL)` directly**
  — without `-DOPENSSL_ROOT_DIR=/usr/local/opt/openssl3`, it finds the
  same ancient stock OpenSSL. Always pass both `OPENSSL_ROOT_DIR` and
  `CMAKE_PREFIX_PATH`.
- **`htonll` doesn't exist on Tiger.** libgit2's NTLM auth module
  (`USE_NTLMCLIENT`, on by default on Unix) uses it and fails to link
  ("Undefined symbols: _htonll"). Not needed for GitHub (HTTPS token or
  SSH key auth, not NTLM) — just pass `-DUSE_NTLMCLIENT=OFF`.
- Credentials go through libgit2's `git_credential_*` callback (plain
  username+password for HTTPS — e.g. a GitHub PAT as the password — or an
  SSH key file path + passphrase). No ssh-agent or OS keychain dependency;
  the values come straight from `EditorVCSInterface::set_credentials()`,
  which the editor's Version Control dock already has a dialog for.
- The `.dylib` picks up OpenSSL/libssh2 as dynamic dependencies (see the
  `otool -L` list above) rather than statically — matches how CMake
  resolved them by default; not attempted to force static.

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

## Verified working (2026-09-12/13, on a G4 running Tiger 10.4.11)

- Local operations: `_get_vcs_name`, `_initialize`, `_is_vcs_initialized`,
  `_get_modified_files_data`, `_stage_file`, `_commit` all exercised live
  inside the real editor against a real on-disk git repo; commits created
  by the plugin were confirmed independently via `git log` on the same
  repo.
- Remote operations: `_create_remote`, `_get_remotes`, `_remove_remote`,
  and a real `_fetch` against a live public GitHub repo
  (`octocat/Hello-World`) over HTTPS — no credentials needed for a public
  repo, so this genuinely exercises the full TLS handshake +
  git-smart-HTTP protocol + pack download/indexing path end to end on
  real big-endian PPC hardware. Output showed real object counts and
  correctly created remote-tracking refs, no errors.
- **Not yet verified live**: `_push` and `_pull`'s merge path, and
  `_set_credentials`. These need a real authenticated remote (a PAT
  token or SSH key), which wasn't available to test with in the session
  that built this — the underlying mechanism (`credentials_cb`) is a
  straightforward, well-understood libgit2 pattern with nothing
  Tiger-specific about it, but treat this as the one remaining
  real-world gap until someone tries it against a repo they can push to.
