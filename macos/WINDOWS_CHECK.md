# Windows check for TweakXL (mac-full against upstream f8da6be, the v1.11.4 and v1.11.5 tag)

This file lists the 55 shared source files outside `macos/`, `tools/mac/` and documentation that differ between upstream `f8da6be` (v1.11.4 = v1.11.5; R31 merge, 2026-10-09) and the mac-full tip at the time of writing, including the changes of the earlier port by memaxo that this branch carries (each row names its origin), and says whether each change can affect the Windows build or Windows behaviour. It is a static comparison (Windows cannot be built here): each file was reduced to the text a Windows compiler sees (`__APPLE__` undefined, `_WIN32` defined) and compared with upstream, and every file with a remaining difference was read by hand. Result: 13 modified files are fully Apple-guarded, 1 is an ignore-rule change, 10 new files are unreachable from the Windows build or reduce to the old include, and 31 changes are unguarded, all NO-OP.

## Summary table

| Measure | Files |
|---|---|
| Shared files changed outside macos/, tools/mac/ and documentation | 55 |
| Modified, Apple-guarded only (Windows-visible text identical to upstream) | 13 |
| Modified, non-code (ignore rules only) | 1 |
| Additions only (not compiled on Windows, or reducing to the old include) | 10 |
| Unguarded changes (modified files and compiled additions) | 31 |
| of which NO-OP (same behaviour on Windows) | 31 |
| of which CHECK, low risk (needs a Windows build to confirm) | 0 |
| of which CHECK, breaks or may break the Windows build | 0 |

## Shared files changed

NONE: every hunk is inside an `__APPLE__` branch, or a `_WIN32`/`#else` split whose Windows branch is the upstream text, or the file is new and unreachable from the Windows build. NO-OP: unguarded, but the same behaviour on Windows (reason given). CHECK: unguarded behaviour or compile change that Windows CI should build and review.

| file | kind | Windows impact | one-line reason |
|---|---|---|---|
| `.gitignore` | modified | NONE | Drops the `CMakeLists.txt` ignore rule (the new root CMakeLists.txt is tracked) and adds `/build*/`; no build input. [origin: memaxo and codeman101] |
| `CMakeLists.txt` | added | NONE | New root CMake file for the Mac build (it also has a Windows branch); xmake.lua stays the Windows build and does not read it. [origin: memaxo and codeman101] |
| `lib/Core/Facades/Runtime.cpp`<br>`lib/Core/Facades/Runtime.hpp`<br>`lib/Core/Foundation/RuntimeProvider.cpp` | modified | NONE | Mac overloads (`IsASI(void*)`, `IsEXE(std::string_view)`, ctor taking `void*`) added in the `#else` of `_WIN32`; the Windows overloads are untouched. [origin: memaxo] |
| `lib/Core/Foundation/Application.cpp` | modified | NO-OP (risk: none) | Adds a boot trace to stderr in `Bootstrap` that is silent unless `TWEAKXL_BOOT_TRACE` or `TWEAKXL_MAC_DIAG` is set; `OnStarting`, the features loop and `OnStarted` run as before. [origin: memaxo and codeman101] |
| `lib/Core/Foundation/RuntimeProvider.hpp` | modified | NO-OP (risk: none) | `#include "Core/Win.hpp"` replaced by `Core/Platform.hpp`, which includes Win.hpp on Windows. [origin: memaxo] |
| `lib/Core/Hooking/Detail.hpp` | modified | NONE | R26: Apple-only per-hook call counters (the include of `HookHits.hpp`, a call in `Attach`, a bump in `Handle`, a static counter member); every hunk is inside `#if defined(__APPLE__)`. |
| `lib/Core/Hooking/HookHits.hpp` | added | NONE | New (R26). The whole content is inside `#if defined(__APPLE__)`; only `Detail.hpp` includes it, and only on Apple. |
| `lib/Core/Logging/LoggingAgent.cpp` | modified | NO-OP (risk: none) | `s_driver = nullptr` (already zero), a null check in `LogFlush`, and env-gated stderr traces; differs only when no driver was set. [origin: memaxo and codeman101] |
| `lib/Core/Platform.hpp` | added | NONE | New. On Windows it only includes `Core/Win.hpp`, which is what the including headers did before; the `#else` includes macOS.hpp. [origin: memaxo] |
| `lib/Core/Raw.hpp` | modified | NO-OP (risk: none) | Adds `#include "Core/Platform.hpp"` (Win.hpp on Windows). [origin: memaxo] |
| `lib/Core/Runtime/HostImage.cpp` | modified | NO-OP (risk: none) | The six getters (GetBase, GetPath, GetName, GetRootDir, GetFileVer, GetProductVer) moved below the platform `#if` so both platforms share them; bodies identical to upstream. [origin: memaxo] |
| `lib/Core/Runtime/HostImage.hpp`<br>`lib/Core/Runtime/ModuleImage.hpp`<br>`lib/Core/Runtime/OwnerMutex.hpp` | modified | NO-OP (risk: none) | Include of `Core/Win.hpp` replaced by or extended with `Core/Platform.hpp` (Win.hpp on Windows). [origin: memaxo] |
| `lib/Core/Runtime/ModuleImage.cpp` | modified | NO-OP (risk: none) | Adds `#include <iostream>`. [origin: memaxo] |
| `lib/Core/Runtime/OwnerMutex.cpp` | modified | NO-OP (risk: none) | Adds `#include <Windows.h>` in the Windows branch (the file already used Win32 mutex calls). [origin: memaxo] |
| `lib/Core/Stl.hpp` | modified | NO-OP (risk: none) | Adds standard container includes and comments, and moves the `SortedSet` alias below the platform split; the TiltedPhoques and hopscotch aliases are unchanged. [origin: memaxo] |
| `lib/Core/Win.hpp` | modified | NO-OP (risk: none) | Two comment lines added to the Windows part; the Windows includes are unchanged. [origin: memaxo] |
| `lib/Core/macOS.hpp` | added | NONE | Mac platform helpers; reachable only from non-Windows branches (Platform.hpp, Win.hpp, HostImage.cpp). [origin: memaxo] |
| `lib/Red/Specializations.hpp` | modified | NONE | R28: the `std::hash<TweakDBID>` hunk is inside `#if defined(__APPLE__)` (hash only the name hash and the length, the same fields `operator==` compares; the Windows branch is the upstream line `return aKey.value`). Without it a flat that sits in a batch with its TweakDB value offset is not found by its plain id on Mac and an override and the inherited flat both end up in the batch. |
| `lib/Red/TypeInfo/Definition.hpp` | modified | NO-OP (risk: none) | R31: the type virtuals of `ClassDescriptorDefaultImpl` (`IsEqual`, `Assign`, `ConstructCls`, `DestructCls`) take `Detail::InstancePtr` / `Detail::ConstInstancePtr`, which are `void*` / `const void*` on Windows (upstream's spelling) and the Mac SDK's `ScriptInstance` on Apple; same signatures on Windows. Same text as Codeware's and ArchiveXL's copy. |
| `lib/Red/TypeInfo/Registrar.hpp` | modified | NO-OP (risk: none) | Adds `<cstdio>`, `<cstdlib>` and three unused static inline helpers (`RunPending`, `PendingRegisterCount`, `PendingDescribeCount`). [origin: codeman101] |
| `lib/Red/TypeInfo/Resolving.hpp` | modified | NO-OP (risk: none) | `IsScopedEnumName` becomes a template on `T` (T only matters in the Apple branch); the `"enum RED4ext::"` prefix and the result are unchanged; loop index typed `size_t` instead of `auto`. [origin: memaxo and codeman101] |
| `lib/Support/MinHook/MinHookProvider.cpp`<br>`lib/Support/MinHook/MinHookProvider.hpp` | modified | NO-OP (risk: none) | One comment line added. [origin: memaxo] |
| `lib/Support/RED4ext/RED4extProvider.hpp` | modified | NO-OP (risk: none) | R31: upstream's text (upstream removed `RegisterNeverRefType`, `RegisterMixedRefType` and `ConfigureScripts`), plus `#include "Support/RED4ext/V1Compat.hpp"` (empty on Windows) and a `_WIN32` split in `RegisterScripts` whose Windows branch is upstream's line. [origin: memaxo and codeman101] |
| `lib/Support/RED4ext/V1Compat.hpp` | added | NONE | R31. The whole content is inside `#if defined(__APPLE__)`: the `RED4ext::v1` names and `RED4EXT_V1_*` macros of RED4ext.SDK 1.0 mapped onto the Mac SDK port's 0.5 API (same layouts; `RED4EXT_API_VERSION_1_COMPAT_0` is 0 on both). |
| `lib/Support/Spdlog/SpdlogProvider.cpp`<br>`lib/Support/Spdlog/SpdlogProvider.hpp` | modified | NO-OP (risk: none) | Adds `m_resolvedLogPath` (assigned, unused on Windows), `m_logMutex` and `<mutex>`; `<fstream>` and `<iostream>` are Apple-only since R31. The Windows sink is upstream's rotating file sink (1.11.4). The Mac file writer has no rotation. [origin: memaxo] |
| `lib/Support/macOS/MacExit.hpp`<br>`lib/Support/macOS/MacOSHookingProvider.hpp`<br>`lib/Support/macOS/TweakXLAddressResolver.hpp` | added | NONE | Mac hooking provider and address resolver declarations; unguarded headers, but only the `#else` block of `src/App/Application.cpp` and the .cpp below include them. [origin: memaxo and codeman101] |
| `lib/Support/macOS/TweakXLAddressResolver.cpp` | added | NONE | Mac address table; the whole file is wrapped in `#if defined(__APPLE__)`, so the xmake `lib/**.cpp` glob compiles an empty unit. [origin: memaxo and codeman101] |
| `src/App/Application.cpp` | modified | NO-OP (risk: none) | Include order moved and an empty `BootTrace()` stub added; the Windows constructor and `OnStarting` equal upstream. [origin: memaxo and codeman101] |
| `src/App/Application.hpp` | modified | NO-OP (risk: none) | Adds `#include "Core/Platform.hpp"`. [origin: memaxo] |
| `src/App/Environment.hpp` | modified | NONE | Narrow-string path overloads in the `#else`; the `L"..."` Windows paths stay in the `_WIN32` branch with the same text. [origin: memaxo] |
| `src/App/MacRegisterNatives.cpp` | added | NONE | Mac native registration; the whole file is wrapped in `#if defined(__APPLE__)`, so the xmake `src/**.cpp` glob compiles an empty unit. [origin: codeman101] |
| `src/App/Stats/StatService.cpp`<br>`src/App/Stats/StatService.hpp` | modified | NONE | Apple-only StatService port (optional hooks, shutdown unhooking); the Windows text equals upstream. [origin: memaxo and codeman101] |
| `src/App/Tweaks/Batch/TweakChangeset.hpp` | modified | NONE | Apple-only async commit on the job queue and diagnostics. [origin: codeman101] |
| `src/App/Tweaks/Executable/TweakExecutor.cpp` | modified | NO-OP (risk: none) | Apple-only diagnostics, and (R25, unguarded) a null guard for an unregistered `ScriptableTweak`: `GetClasses` with a null base returns every class, so the guard returns with an error instead; with `ScriptableTweak` registered, as on every normal install, the behaviour is the upstream one. Fix found in jackmaxwil/cp2077-tweak-xl-macos, MIT. [origin: codeman101] |
| `src/App/Tweaks/Metadata/MetadataImporter.cpp` | modified | NO-OP (risk: none) | (R25, unguarded) the `propName` buffer is 256 bytes instead of 254; `propNameLen` is a `uint8_t` that reaches 255. Fix found in jackmaxwil/cp2077-tweak-xl-macos, MIT. [origin: codeman101] |
| `src/App/Tweaks/Declarative/Yaml/YamlReader.Template.cpp` | modified | NO-OP (risk: none) | `it.value().X` becomes `it->second.X` (read-only); upstream line 154 of the same file already uses `it->second` on the same hopscotch map. [origin: memaxo] |
| `src/App/Tweaks/TweakContext.hpp`<br>`src/App/Tweaks/TweakService.hpp` | modified | NONE | Apple-only variants (Episode One flag from the loaded path, shutdown override); the Windows text equals upstream. [origin: memaxo and codeman101] |
| `src/App/Tweaks/TweakService.cpp` | modified | NO-OP (risk: none) | Adds `<cstring>`, `<cstdio>`, `<exception>`; the Windows hooks equal upstream, the Mac variants are Apple-only (R31: the Mac `OnBootstrap` calls `ReportUsage()` and `LoadTweaks()` like upstream 1.11.4). [origin: memaxo and codeman101] |
| `src/Red/StatsDataSystem.hpp` | modified | NO-OP (risk: none) | Adds `#include <type_traits>`; the rest is Apple-only. [origin: codeman101] |
| `src/Red/TweakDB/Manager.cpp` | modified | NO-OP (risk: none) | `it.value()->type` becomes `it->second->type` (read-only access) and one `const auto propId` becomes `auto propId`. [origin: memaxo and codeman101] |
| `src/Red/TweakDB/Raws.hpp` | modified | NONE | Apple-only Mac signatures for the stats and TweakDB functions; Windows declarations unchanged. [origin: codeman101] |
| `src/Red/TweakDB/Reflection.cpp` | modified | NO-OP (risk: none) | Adds `#include <iostream>` and `extraFlatsIt.value()` becomes `extraFlatsIt->second` (read-only loop). [origin: memaxo and codeman101] |
| `src/Red/TweakDB/Source/Parser.cpp` | modified | NO-OP (risk: none) | `throw std::exception(msg.c_str())` (MSVC-only constructor) becomes `throw std::runtime_error(msg)`; same message, derived type still caught by `const std::exception&`. [origin: memaxo] |
| `src/pch.hpp` | modified | NONE | R31: `#include "Support/RED4ext/V1Compat.hpp"` inside `#if defined(__APPLE__)`. |
| `src/main.cpp` | modified | NO-OP (risk: none) | Adds `<iostream>`, `<cstdlib>` and a comment; the Windows `Main` equals upstream, the Mac variant is Apple-only. R27: the Mac variant leaves `g_app` alone at Unload, flushes its log and calls `Mac::ExitWhenAllPluginsUnloaded` (all inside `#if defined(__APPLE__)`; `Support/macOS/MacExit.hpp`). [origin: memaxo and codeman101] |

## Remaining items for Windows CI

No known Windows compile break and no open CHECK item. The `vendor/RED4ext.SDK` gitlink is upstream v1.11.5's pin `6ea7425a` (psiberx/RED4ext.SDK, public) and `.gitmodules` is upstream's, `config/Project.hpp.in` is the upstream xmake template, and the Mac-only `.cpp` files are wrapped in `#if defined(__APPLE__)`. A Windows CI build (xmake) should still run once.

Small unguarded differences that remain on the Windows side, all rated NO-OP, risk: very low:

- `lib/Core/Foundation/Application.cpp`, `lib/Core/Logging/LoggingAgent.cpp`: read `TWEAKXL_BOOT_TRACE` / `TWEAKXL_MAC_DIAG` with `std::getenv` and print to stderr only when set; `LogFlush` now tolerates a missing driver.
- `lib/Red/TypeInfo/Definition.hpp` (R31): the type virtuals spelled with `Detail::InstancePtr` (`void*` / `const void*` on Windows).
- `src/Red/TweakDB/Source/Parser.cpp`: `std::runtime_error(msg)` instead of the MSVC-only `std::exception(const char*)`; the exception type is narrower, the message is the same.
- `lib/Support/Spdlog/SpdlogProvider.hpp`: two new members (`m_resolvedLogPath`, `std::mutex m_logMutex`), unused on Windows; `lib/Core/Runtime/OwnerMutex.cpp`: extra `#include <Windows.h>`.
