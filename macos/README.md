# TweakXL for macOS (Apple Silicon)

A port of [psiberx/cp2077-tweak-xl](https://github.com/psiberx/cp2077-tweak-xl) (1.11.5, upstream commit f8da6be, the same commit as 1.11.4) to the native Apple-Silicon build of Cyberpunk 2077 (Steam,
game 2.3.1, arm64). Windows is the reference build (20383525). The first macOS port is by memaxo; this tree continues it (address evidence, the
StatsDataSystem Mac ABI, the job queue, the build and the launcher integration by codeman101 / skyman3215). MIT, like upstream.

This file describes the Mac layer in `macos/`, `lib/Support/macOS/` and `tools/mac/`. The rest of the tree is upstream code with `__APPLE__` branches
where the Mac ABI or the Mac game differs: `macos/WINDOWS_CHECK.md` lists every shared file that differs from upstream, for review before a Windows build.

Upstream builds against psiberx/RED4ext.SDK 1.0 (the `RED4ext::v1` plugin API). The Mac build uses the sibling RED4ext.SDK-macos, which is still on
the 0.5 API with the same structure layouts; `lib/Support/RED4ext/V1Compat.hpp` maps the v1 names onto it (Apple only), so the shared sources keep
upstream's spelling. The plugin reports API version 0 on both (`RED4EXT_API_VERSION_1_COMPAT_0`). The Mac build file (`CMakeLists.txt`) sets the
version string 1.11.4, the one upstream's `xmake.lua` has at the v1.11.5 tag (the build stops if the two differ), so the Mac build reports
what upstream's own build of that tag reports.

## What works (seen in menu boots of the source build)

| feature | evidence |
|---|---|
| Tweak discovery and import (`r6/tweaks/**/*.yaml`, `*.tweak`) | `Import completed.` with Lizzie's BDs and the session-only self-test YAML |
| Scriptable tweaks (Equipment-EX: `PatchOriginaltems`, `PatchCustomItems`, `RegisterOutfitSlots`) | `Executing "EquipmentEx...."` and no `doesn't have OnApply` |
| Custom stat types (`BaseStats.*` records) | `StatService: custom stat type <name> applied (enum value N).` and the self-test that reads it back through the three getters |
| Batch commit on the game job queue (`TweakChangeset`) | `[selftest] async commit: chunk running on a job thread` and `chunk finished` |
| TweakDB, flats, records, inheritance and extra flats metadata | every boot |

## Requirements

- macOS 14 or newer, Apple Silicon, Xcode command line tools, CMake 3.20 or newer, Homebrew `spdlog`, `fmt`, `yaml-cpp`.
- The sibling checkout `../RED4ext.SDK-macos` on its `mac-full` branch (or `-DRED4EXT_SDK_DIR=<include dir>`). The `vendor/RED4ext.SDK` submodule in this tree is the upstream (Windows) pin and is not used by the Mac build: its `SharedSpinLock` follows the Windows lock protocol and would corrupt the game's own locks (the stats lock at `StatsDataSystem+0xFC`, the TweakDB mutexes).
- RED4ext-macos with the Frida gadget, and the hook allowlist described below.

## Build

From the parent folder of the three repos: `tools/build-all.sh` (builds Codeware, TweakXL and ArchiveXL, signs them, writes sha256 sums). For this
repository alone:

```sh
cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release
cmake --build build-macos -j
```

The post-build steps rewrite the install names of `libfmt`, `libspdlog` and `libyaml-cpp` to `@loader_path` (`macos/fix-install-names.sh`), ad-hoc sign
the dylib, and run `macos/check-undefined.sh`, which fails the build when the dylib needs a symbol of the TweakXL namespaces that it does not define.
Copy `TweakXL.dylib` and the three libraries next to it into `<game>/red4ext/plugins/TweakXL/`.

## Runtime hooks and the allowlist

RED4ext-macos installs a detour only on offsets listed in `RED4EXT_GUM_HOOK_OFFSETS`. For any other offset `Attach` reports success and hands back the
target itself as the original, which is a silent no-op. `MacOSHookingProvider` detects that and logs `hook NOT installed (offset not on the GUM_OFFSETS
allowlist)`, so a missing offset is visible. The offsets TweakXL needs (image-relative):

| offset | function | when |
|---|---|---|
| `0x35f1bac` | `TweakDB_Init` (job body) | always |
| `0x2b7cc9c` | `TweakDB_TryLoad` | always |
| `0x3a939a0` | `StatsDataSystem_InitializeRecords` | always (custom stat types) |
| `0x3a9472c`, `0x3a93ef0`, `0x3a93e74` | `GetStatRange`, `GetStatFlags`, `CheckStatFlag` | installed lazily, only when a tweak adds a custom stat |

`0x35f1bac` and `0x2b7cc9c` are on the base allowlist of the test launcher; the four stat offsets are added by it for a TweakXL session.

## Environment variables

| variable | effect |
|---|---|
| `TWEAKXL_MAC_DIAG=1` | diagnostics and self-tests (R8 and R11 traces, `[selftest]` lines, `[stat]` lines, hook INSTALLED lines). Default: off. |
| `TWEAKXL_MAC_ASYNC_COMMIT=0` | run the dispatched commit chunk synchronously instead of on the game job queue. Default: job queue. |
| `TWEAKXL_ADDR_TRACE=1` | print every address the resolver hands out. Default: off. |

## Mac ABI differences handled in the code

- `StatsDataSystem::GetStatRange` returns `{min, max}` in `s0:s1` on the Mac (a homogeneous float aggregate), with no hidden result pointer. The Raw
  declaration and the hook handler use `Red::StatRange (*)(void*, uint32_t)` (`src/Red/StatsDataSystem.hpp`). `GetStatFlags` and `CheckStatFlag` are unchanged.
- On the Mac `CheckStatFlag` has `GetStatFlags` inlined, so both are hooked.
- The Raw function objects resolve to the original once a hook is attached; the game's own virtual call path is the way to exercise a hook (the
  self-test calls through the vtable slots `0x208`, `0x210`, `0x218`).
- `TweakDBID_Derive` has no row; `CreateTweakDBID` is computed in C++ (CRC32 with the base hash as seed), which equals the game function at `0x34535C0`.
- The enum name `gamedataStatType` comes from the clang spelling of the SDK enum (`lib/Red/TypeInfo/Resolving.hpp`).
- The job runtime rows (`JobQueue`, `JobDispatcher`, `JobHandle`) come from `macos/MacSdkRows.inc`, injected into a generated copy of the SDK
  `Relocation-inl.hpp` at configure time. The sibling SDK checkout is not modified.

## Layout guard

`macos/MacLayoutGuard.cpp` is generated by `tools/mac/gen-mac-layout.py` from the game's reflection dump and fails the build when a member of an SDK class that
TweakXL touches sits at a different offset on the Mac. It currently checks 21 classes and 32 members with no mismatch. `tools/mac/check-sdk-layout.py`
checks every class of the SDK against the dump (run it after a change of the SDK headers).

## Hook allowlist file

`macos/hook-offsets.txt` lists the offsets above in the format `tools/build-all.sh` merges into `GUM_OFFSETS.txt`.

## Known gap: RED4ext's own exit code crashes (worked around in the plugin)

The installed RED4ext.dylib (NightCity Console build) crashes at the end of its own exit code on every normal quit: App::Destruct opens a DetourTransaction, does not commit it, and the destructor logs through an spdlog default logger that is already null (RED4ext.dylib+0xdb134; the same stack is in the 2026-10-03 crash reports, before any of our plugins). On Apple the last of our three plugins to unload leaves the process with _exit(0), only while the process is exiting (the Unload call must be inside libsystem_c's `exit()`, checked on the stack with backtrace and dladdr), after flushing its logs. Mac only (macos/MacExit.hpp). A plain Unload (reload, failed load, one plugin alone) never exits.
The plugin's `Main` (Unload) also leaves its application and extension objects alone (`g_app.release()`): they hold tokens and handles of the game, and
destroying them from RED4ext's atexit handler went through freed SDK relocation tables (the r27 quit crash, fixed in the SDK as well). Environment keys:
`CP2077_MAC_PLUGIN_AXL_STATE`, `CP2077_MAC_PLUGIN_CW_STATE`, `CP2077_MAC_PLUGIN_TXL_STATE` ("loaded" / "unloaded").

## Files

- `macos/ADDRESS_CHECK.md`: every address row and field offset TweakXL uses, checked against the Windows twin.
- `macos/EVIDENCE_STATS_TWEAKDB.md`: the disassembly evidence for the five StatsDataSystem rows, the struct fields and the TweakDB raws.
- `macos/INGAME_TESTS.md`: what only shows in game. `macos/NOT_YET.md`: known gaps.
