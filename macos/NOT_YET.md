# Known gaps and caveats of the TweakXL macOS port

| area | state | why / what happens |
|---|---|---|
| Custom stat getter hooks are installed lazily | works in menu boots | `GetStatRange`, `GetStatFlags` and `CheckStatFlag` are hooked from the `InitializeRecords` handler, which runs on a game job thread, so other threads can call a getter while the hook goes in (the Windows code does the same). Frida-gum patches the entry atomically and the hook was installed this way in every self-test boot. A session with no custom stat never installs them. A failed install is logged as an error, never thrown. |
| Two entries start with a PC-relative branch | works | `GetStatRange` (`b.hi` at +0xC) and `GetStatFlags` (`b.hi` at +4) begin with `cmp w1,#0x6ac; b.hi`. The hook engine relocates the branch; the self-test calls both through the vtable and gets the right values. |
| `GetStatFlags` hook does not see `CheckStatFlag` calls | by design | the Mac `CheckStatFlag` has `GetStatFlags` inlined. Both are hooked. |
| Stat offsets must be on the hook allowlist | needs setup | without `0x3a939a0` (and later the three getters) the plugin logs `hook NOT installed (offset not on the GUM_OFFSETS allowlist)` and `Stats hooks unavailable`; TweakXL still imports tweaks, only custom stat types are off. |
| `TweakDBID_Derive` row | replaced | no Mac row in the table; `CreateTweakDBID` is computed in C++ (equals the game function at `0x34535C0`). |
| `Main` row (`0x31e18`) | unused on Mac | the plugin entry is the RED4ext-macos `Main` export; nothing calls the row. |
| `vendor/RED4ext.SDK` | not used on Mac | the upstream Windows pin; it has the Windows `SharedSpinLock` protocol. The Mac build uses the sibling `RED4ext.SDK-macos` (`mac-full`). |
| Not run on Mac yet | untested | `TweakXL.Reload()` after boot, `ExportMetadata`, `Require`, `.tweak` (source format) files with inheritance across packages, tweaks that add records of every type, reading a custom stat in a running game session (the stat is registered at the menu, but no game mechanic ran against it). |
| Upstream test and build files | present | `xmake.lua` and the Windows build files are untouched and unused on Mac. |
