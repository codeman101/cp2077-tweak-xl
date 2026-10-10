# In-game tests for TweakXL (end checklist)

Run the game through `launch_phase6.sh` (the source-built TweakXL is the default for a session; the launcher parks the installed `08f38876` build and
puts it back at the end; `TWEAKXL_MAC_DIAG=1` is on). The launcher prints `Logs (send these): /tmp/cp2077-phase6-<stamp>-*`; the `[TweakXL ...]`
lines are in the `-launch.log` file and in the `-tweakxl.log` copy. "Seen at the menu" means seen in a menu boot of the source build.

| feature | what to do | proof in the log |
|---|---|---|
| Tweaks import | nothing; runs at boot | `Importing tweaks...` ... `Import completed.`. Seen at the menu. |
| Equipment-EX scriptable tweaks | open the wardrobe and the outfit slots in game | `Executing "EquipmentEx.PatchOriginaltems"...`, `PatchCustomItems`, `RegisterOutfitSlots`, `Execution completed.`; no `doesn't have OnApply`. Seen at the menu. Wardrobe and outfit slots work in game. |
| Lizzie's items and prices (`LizziesBDs/*.yaml`) | buy or find a Lizzie's item | `Reading "LizziesBDs/Items.yaml"...`. Seen at the menu. The one known warning `Character.LizziesBDs_Pic_Lucy.entityTemplatePath refers to a non-existent resource.` shows on every boot of this setup, also with the older TweakXL build, so it is not a regression. |
| Custom stat types | nothing for the session-only test; a real mod adds a `BaseStats.<Name>` record | `StatService: custom stat type <Name> applied (enum value N).`, `stat getter hooks installed`, and with diag `[selftest] stat getters <Name> ... PASS`. Seen at the menu (r23e and later). In game, a stat modifier that targets the custom stat must apply. |
| Batch commit on the job queue | nothing | `[selftest] async commit: chunk running on a job thread`, `chunk finished`. Seen at the menu. |
| Null guard for an unregistered `ScriptableTweak` (R25) | nothing; diag self-test at boot | `[selftest] R25 GetClasses(null base) returns <N> classes, GetClasses(ScriptableTweak) returns <M> (PASS)` with N above 1000 and M the number of tweak classes, and no `ScriptableTweak is not registered`. Seen at the menu (r25a: 16166 against 3). |
| Zeroed tail of the grown TweakDB flat buffer (R25) | nothing; diag self-test at boot | `[selftest] R25 flat buffer tail: used=<n> capacity=<n> scanned=<n> bytes nonzero qwords=0 PASS`. Seen at the menu (r25a: 4 MiB behind the last flat scanned, 0 non-zero words). |
| YAML records with `$base` and inline LocKey / bool / int flats (R28) | load a save with Lizzie's BDs installed; `tools/autotest` scenario `lizzie` | `lizzie.tweak_yaml_flats_match`: every inline flat of the mod's yaml files (34: 24 LocKey, 8 bool, 2 int) reads back from TweakDB with the yaml value. Before the R28 `std::hash<TweakDBID>` fix 30 of 34 did: a flat held in a batch with its TweakDB offset in the top 24 bits of the id was not found by its plain id, so an override and the inherited flat both ended up in the batch (`Character.LizziesBDs_Performer_Base.displayName` read the parent's value, `q50_main_LizziesBDs.fullDisplayName` read 0). Seen 34 of 34 in the R28 gate runs. |
| New game or load a save with tweaked items | start a NEW slot, load, play a minute | no `[crash]`; items of the YAML tweaks show their changed stats. |

If one fails, send the `-launch.log` and `-tweakxl.log` files of the session.
