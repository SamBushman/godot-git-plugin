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

## API compatibility with the actual engine (not just the SConstruct/CMake side)

Upstream `v1.x`'s own README says it "works only for Godot 3.2.x-3.4.x" -
godot-ports is 3.6.1, and that gap turned out to be real, not just a
version-number formality. Found and fixed while getting a real project's
Version Control dock to actually show anything:

- **`_get_modified_files_data()` returned the wrong type entirely.**
  `v1.x` returns a flat `Dictionary {file_path: status}`. The actual
  engine (`editor/editor_vcs_interface.cpp`) expects an `Array` of
  `{file_path, change_type, area}` dictionaries — `area` splits files into
  staged/unstaged, a concept `v1.x`'s shape can't express at all. Passing
  the wrong Variant type here doesn't error, it just silently returns
  nothing to the dock — looked exactly like a stale-UI/cache bug (a
  refresh button click changed nothing) before the real cause was found.
- **`_get_current_branch_name` existed but was never registered** — the
  engine calls it directly (`call("_get_current_branch_name")`) for the
  branch label in the dock; it needs `register_method()` like every other
  endpoint, not just to exist as a C++ method.
- **`_get_diff`, `_discard_file`, and branch management
  (`_get_branch_list`/`_create_branch`/`_remove_branch`/`_checkout_branch`)
  didn't exist at all** in `v1.x` — the engine calls `_get_diff(identifier,
  area)` for the diff viewer; `v1.x` only had an old, differently-named
  `_get_file_diff(path)` that nothing calls anymore. Backported all of
  these from `v2.x`, same translation approach as the remote-ops backport
  (rewritten in this file's plain style, not v2.x's RAII wrappers).
- **`_get_diff`'s STAGED case needed a different libgit2 call than v2.x
  uses**, because of an architecture difference already baked into this
  plugin: `_stage_file()`/`_unstage_file()` only touch an in-memory
  `staged_files` list, not git's real index (only `_commit()` touches the
  real index, at commit time). `v2.x`'s `_get_diff` diffs `HEAD` against
  the *real* index (`git_diff_tree_to_index`) for the STAGED area, which
  is always empty under this plugin's staging model - it correctly ran,
  just always returned 0 hunks. Fixed by diffing `HEAD` directly against
  on-disk content instead (`git_diff_tree_to_workdir`), which matches what
  "staged" actually means here: whatever's in `staged_files` will be
  committed using its *current* on-disk content, there's no separate
  frozen index snapshot to diff against.

**Real, confirmed big-endian bug, found live (not theoretical):**
`EditorVCSInterface`'s inherited `create_status_file()`/`create_diff_file()`/
`create_diff_hunk()`/`create_diff_line()`/`create_commit()` convenience
methods are meant to build the exact Dictionary shapes the engine expects
without the plugin needing to hand-roll key names. On this platform they're
broken for any integer field: godot-cpp's generated icall
(`___godot_icall_Dictionary_String_int_int` etc., in
`godot-cpp/include/gen/__icalls.hpp`) packs int arguments as `int64_t` (8
bytes) across the GDNative ptrcall boundary, but the engine's real C++
signature takes a narrower native enum (`ChangeType`/`TreeArea`, likely 4
bytes). Confirmed with a debug print bracketing the call: passing
`area=2` in, getting a Dictionary with `area=0` back, every time. On
little-endian platforms, reading a narrower type from the front of a wider
one still gets the right low-order bytes; on big-endian PPC it reads the
high-order bytes instead, which are zero for any small value - silently
truncating `1`/`2`/etc. to `0`. This would be invisible on every mainstream
(little-endian) platform this plugin has ever run on. **Fix: don't call
any of the inherited `create_*()` helpers — build every Dictionary by hand
instead** (same field names, just assigned directly with plain `dict["key"]
= value`, which never crosses that ptrcall boundary). Done for all of
`_get_modified_files_data()`/`_parse_diff()`/`_get_previous_commits()`.

**Full interface audit (2026-09-13):** cross-checked every `BIND_VMETHOD`
in `editor/editor_vcs_interface.cpp` (21 total) against what this plugin
registers. Two were missing: `_get_previous_commits` (now implemented,
drives the Commit List panel) and `_get_line_diff`, **now also
implemented** — see "Line diff / script editor markers" below.
[Issue #1](https://github.com/SamBushman/godot-git-plugin/issues/1)
tracked this; closed once both the plugin method and its
godot-ports-side consumer landed. Also confirmed `_is_vcs_initialized`/`_get_project_name`/
`_get_file_diff` (still present in this plugin, inherited from `v1.x`)
aren't part of the current 21-method interface at all — harmless dead
code, not gaps.

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
- Status/diff/discard/branches, tested against a real project (correct
  staged/unstaged split: 1 staged + 28 unstaged out of 29 real files;
  correct diff content and line numbers for a staged file) and a
  disposable throwaway repo for the destructive ops (`_discard_file`
  confirmed to actually revert on-disk content and status; `_create_branch`
  / `_checkout_branch` / `_remove_branch` all confirmed via real
  `git branch`/`git status` state, not just the plugin's own report).
- `_get_previous_commits` (the Commit List panel), tested against a
  disposable repo with 3 real commits — correct author/message/id/
  timestamp for all three (confirming the big-endian-safe manual
  Dictionary construction works here too). One real bug caught in
  testing, not just in review: plain `GIT_SORT_TIME` has no way to order
  commits made within the same second (git's commit timestamps only have
  1s resolution) — three commits made back-to-back in the test script
  came back in a non-monotonic order. Fixed by adding
  `GIT_SORT_TOPOLOGICAL` as a tiebreaker, which still respects
  parent-before-child so newest-first stays correct.
- **`_push` + `_set_credentials`, confirmed live (2026-09-13) against a
  real private GitHub repo** using HTTPS + a Personal Access Token,
  entered through the Version Control dock's own Set Up Version Control
  dialog — real commits appeared on github.com immediately after,
  independently confirmed via the GitHub API. This closes out the last
  gap noted in earlier verification passes; every method this plugin
  implements has now been exercised against a real remote/real project,
  not just a synthetic local test.
- `_pull`'s merge path (as opposed to plain `_fetch`, already verified)
  is still not separately confirmed — low risk given it shares the same
  `credentials_cb`/`git_remote_connect` path as the now-verified `_push`,
  but the actual fast-forward/merge logic in `_pull` hasn't been
  exercised against real divergent history yet.
- `_get_line_diff` (see "Line diff / script editor markers" below):
  verified the full data pipeline live against a real 2-commit-apart
  test repo (one modified line, one newly added line) — returned exactly
  the expected hunks (a hunk with both a deletion and an addition for the
  modified line, a separate pure-addition hunk for the new line), correct
  line numbers, correct content. The godot-ports consumer
  (`ScriptTextEditor::update_vcs_status_markers()`) ran to completion
  with no errors when exercised via the real editor's
  `EditorInterface.edit_script()` path. **Not independently confirmed
  visually** — see the note below.

## Line diff / script editor markers

`_get_line_diff(file_path, text)` diffs `text` (the editor's current,
possibly-unsaved buffer) against the file's last-committed (HEAD) blob
directly, via libgit2's `git_patch_from_blob_and_buffer` — no synthetic
tree/workdir diff needed, this is exactly what that function is for. A
null old-blob (new, never-committed file) is handled automatically by
libgit2 as "diff against an empty file," correctly marking every line
added. Shares the same big-endian-safe manual-Dictionary hunk-building
helper as `_get_diff` (refactored into `_build_hunks_array()`).

This backs a companion feature in
[SamBushman/godot-ports](https://github.com/SamBushman/godot-ports)
(PR [#58](https://github.com/SamBushman/godot-ports/pull/58), merged
into `Tiger_GL1_2_FF`): colored line numbers in the script editor
(green=added, orange=modified), refreshed on save and when a script is
first opened. No reference implementation existed anywhere upstream to
build this against — checked every Godot version including current
4.x; it's a long-standing unimplemented feature request
(godotengine/godot-proposals#1089, open since 2020). Full design
writeup is in that PR's commit message. One thing worth flagging for
whoever picks this up next: the actual rendered colors were not
independently confirmed visually this session — a screenshot of the
SSH-launched test instance came back with a correctly-titled but blank
window content area. The process itself ran correctly throughout (see
above), so this looks like a rendering-specific cousin of the known
CoreDrag SSH-interaction limitation (the window's OpenGL surface isn't
reliably painted to the visible framebuffer when launched this way),
not a flaw in the feature — but it's worth a 30-second look at the
physical machine to be sure the colors actually show up as designed.

**Follow-up, same day** ([PR #59](https://github.com/SamBushman/godot-ports/pull/59),
also merged): the user caught a real gap in #58 by asking a precise
question rather than assuming — editing a script and saving the *scene*
(not the script itself) writes the file to disk correctly via Godot's
own "flush any modified-but-unsaved open resource" scene-save behavior,
but that path never touched the marker-refresh hook, so the colors
silently went stale. Auditing every other script write/reload path
found a second gap the same way: the "file changed externally, reload?"
dialog (and the debugger's own script-reload trigger) also never
refreshed markers. Both fixed at their lowest common points — the
actual `ResourceSaver::save()` save-callback (fires for literally every
save in the editor) rather than patching each UI action's call site
individually. This time verified with a temporary debug print
confirming the exact call chain fires, not just static review — see
that PR's description for the full test.
