# Firestone Hooks Build / Install Notes

## Build

```bash
./gradlew clean assembleDebug
./gradlew lint
```

Output APK:

```text
app/build/outputs/apk/debug/app-debug.apk
```

The APK package is `com.firestone.hooks`. The LSPosed static scope is `com.HolydayStudios.Firestone`; the module package intentionally remains separate so it does not replace the original game APK.

This module does not implement or install Google Play, Play Store license, billing, sign-in, or login hooks. The old `GooglePlayLicense.Check(...)` offsets documented in `ANALYSIS.md` were evidence from the earlier third-party mod-menu APK only and are intentionally excluded from this LSPosed module.

## Settings JSON

Settings can be changed from either the module launcher Activity or the injected in-game `FS`
floating bubble. The bubble is attached to Firestone's Activity when the LSPosed module is enabled
and scoped to `com.HolydayStudios.Firestone`.

Standalone settings live in the module Activity and are exposed to the target process through:

```text
content://com.firestone.hooks.settings/config
```

When Firestone starts, the module syncs that provider JSON into:

```text
/data/data/com.HolydayStudios.Firestone/files/firestonehooks.json
```

The native library refreshes this file every 500 ms.

`enabled` is retained only for compatibility with older builds. The settings Activity no longer
exposes a master off switch, and the Java provider, target-file sync, and native settings refresh
normalize `enabled` to `true`. Use LSPosed/Vector Manager to disable the module globally; use
`native_hooks` and the per-feature toggles below for runtime control.

The injected `FS` bubble writes changes back through the same provider, so in-game toggles and the
standalone settings Activity stay in sync.

The settings Activity also mirrors the JSON to:

```text
/sdcard/Android/media/com.firestone.hooks/firestonehooks.json
```

This is a fallback for Android package-visibility cases where the target process cannot resolve the
module ContentProvider. If both provider and fallback are unavailable in the target process, the
module uses the safe default JSON above and logs the fallback once.

Default schema:

```json
{
  "enabled": true,
  "native_hooks": true,
  "free_currency": true,
  "god_mode": true,
  "game_speed": false,
  "wave_speed": false,
  "one_hit_kill": false,
  "attack_speed": false,
  "attack_speed_battle_stat": true,
  "attack_speed_idle_timer": true,
  "attack_speed_attack_timer": true,
  "attack_speed_roster_stat": true,
  "slow_enemies": false,
  "game_speed_multiplier": 2.0,
  "wave_speed_multiplier": 2.0,
  "damage_multiplier": 1000.0,
  "attack_speed_multiplier": 2.0,
  "enemy_attack_speed_multiplier": 2.0
}
```

Multiplier ranges: game speed and wave speed are `0.25x..10x`; hero attack speed is `1x..20x`;
enemy attack interval is `1x..25x`; damage multiplier is `1x..1000x` in the UI.

## Install / Scope

1. Install the module:

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

2. In LSPosed Manager:

```text
Enable Firestone Hooks -> Scope -> com.HolydayStudios.Firestone -> force-stop Firestone
```

3. Launch and capture verification:

```bash
adb shell am force-stop com.HolydayStudios.Firestone
adb logcat -c
adb shell am start -n com.HolydayStudios.Firestone/com.unity3d.player.UnityPlayerActivity
adb logcat -v time | grep -E 'FirestoneHooks|LSPosed|XposedBridge|shadowhook|il2cpp|FATAL' | tee artifacts/logcat_launch.txt
adb shell screencap -p /sdcard/firestone_launch.png
adb pull /sdcard/firestone_launch.png artifacts/firestone_launch.png
```

Expected logcat markers:

```text
FirestoneHooks: Loaded libfirestonehooks.so
FirestoneHooks: JNI_OnLoad firestonehooks
FirestoneHooks: libil2cpp.so base=...
FirestoneHooks: EasyWin.CentralCurrencyHandler.HaveCurrency hook installed ...
FirestoneHooks: EasyWin.Currency.HaveCurrency hook installed ...
FirestoneHooks: easy-win FreeCurrency toggle active; affordability hooks installed
FirestoneHooks: native hook install complete result=0
```

## Verified Run

Final device verification on 2026-05-07 used `127.0.0.1:16384` with `com.HolydayStudios.Firestone` installed and only this module package (`com.firestone.hooks`) present for the Firestone scope. A stale older module package (`com.jordan.firestone.lsposed`) was removed and the emulator was rebooted before the final capture.

Artifacts:

```text
artifacts/logcat_launch.txt
artifacts/logcat_launch_full.txt
artifacts/firestone_launch.png
```

The filtered log shows LSPosed loading `com.firestone.hooks`, `JNI_OnLoad firestonehooks`, `libil2cpp.so base=...`, the Easy-Win hooks installed, `easy-win FreeCurrency toggle active; affordability hooks installed`, and `native hook install complete result=0`. No crash markers were present in the filtered launch log.

## Troubleshooting

| Symptom | Likely cause | Action |
|---|---|---|
| Module not loaded | LSPosed module disabled or scope missing | Enable module, scope `com.HolydayStudios.Firestone`, force-stop game. |
| No `JNI_OnLoad firestonehooks` | Native library not loaded in target | Check `xposedmodule=true`, `META-INF/xposed/java_init.list`, and `TemplateConfig.NATIVE_LIBRARY_NAME`. |
| `libil2cpp.so base not found` | Native install ran before Unity loaded IL2CPP | Current worker waits up to 30 s. Re-check logs for late base resolution before changing code. |
| Launch crash after hook install | Risky combat hook ABI or same-page hook conflict | Disable in this order: `one_hit_kill`, `attack_speed`, `game_speed`, then retry. Re-enable one at a time. |
| Game closes after turning off the old in-app `Module enabled` switch | Older builds could persist `enabled=false`, leaving Firestone to launch without module/native logs and then exit on its own PlayCore path | Install the current APK, force-stop `com.firestone.hooks` and Firestone, then relaunch. The setting is normalized back to `true`; use LSPosed Manager for global disablement. |
| Game closes after disabling `com.firestone.hooks` in LSPosed/Vector | The module is not loaded at all; logcat shows no `FirestoneHooks` lines and Firestone exits after its own PlayCore update request | Re-enable the module and scope, then use per-feature toggles for a no-feature run. A globally disabled module cannot change Firestone startup behavior. |
| Crash at `libil2cpp.so+0x2ac2dbc` | Unsafe `BigCurrency.HaveCurrency` function-entry return-skip | The installed build removes this intercept; keep the documented candidate disabled until a typed proxy or call-site patch is validated. |
| Crash only with OHK | `BattleController.ApplyDamage` ABI or target-side guard mismatch | Keep `one_hit_kill=false`; prefer god-mode/free-currency until dynamic register tracing confirms the call-site ABI. |
| Attack speed installs but feels unchanged | Only the stat interval path is active, or the hero was already inside the live attack loop | Enable `attack_speed_idle_timer` and `attack_speed_attack_timer`; these touch the live `CharacterBattleLogic` timers. `attack_speed_roster_stat` updates all ally hero `Hero.attackSpeedBasic` values when the roster stat recalculates. |
| `ObscuredCheatingDetected` appears | Direct `ObscuredFloat` field write detected | Keep `attack_speed=false` or route through the helper at `0x211761C`; current implementation already prefers that helper. |
| Shadowhook duplicate/same-page error | Another module hooked the same RVA | Disable the conflicting module or switch the affected hook to an intercept-only strategy. |
