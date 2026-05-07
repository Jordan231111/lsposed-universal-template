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
  "settings_schema_version": 2,
  "enabled": true,
  "native_hooks": true,
  "free_currency": true,
  "event_exchange_zero_cost": true,
  "event_exchange_local_only": false,
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
  "damage_multiplier": 1000000.0,
  "attack_speed_multiplier": 2.0,
  "enemy_attack_speed_multiplier": 2.0
}
```

Multiplier ranges: game speed is `0.25x..32x`; wave speed is `0.25x..10x`;
hero attack speed is `1x..20x`; enemy attack interval is `1x..25x`; OHK damage uses a BigDouble exponent slider from
`9.99e1000` to `9.99e1000000`. Schema version `2` migrates old saved OHK
`damage_multiplier <= 1000` values to the new default exponent `1000000` once; after that,
deliberately choosing the minimum exponent remains persistent.

## Install / Scope

1. Install the module:

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

For day-to-day use, install the quieter release module instead:

```bash
./gradlew clean assembleRelease
adb install -r app/build/outputs/apk/release/app-release.apk
```

Release builds compile with `BuildConfig.VERBOSE_LOGS=false` and native
`TEMPLATE_VERBOSE_LOGS=0`, so the `FirestoneHooks` hook-install / hit logs are intentionally
silent. Use the overlay bubble or feature behavior to confirm runtime load; reserve the debug APK
for detailed hook diagnostics.

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
FirestoneHooks: EasyWin.BigCurrency.HaveCurrency hook installed ...
FirestoneHooks: EasyWin.BigCurrency.RemoveCur hook installed ...
FirestoneHooks: EasyWin.CentralCurrencyHandler.PayCurrency hook installed ...
FirestoneHooks: EasyWin.CentralCurrencyHandler.HaveCurrency hook installed ...
FirestoneHooks: EasyWin.CentralCurrencyHandler.HaveResolvedCurrency hook installed ...
FirestoneHooks: EasyWin.Currency.HaveCurrency hook installed ...
FirestoneHooks: EasyWin.Currency.RemoveCur hook installed ...
FirestoneHooks: easy-win FreeCurrency toggle active; affordability + no-spend hooks installed
FirestoneHooks: event exchange zero-cost hooks active; localOnly=0
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

The filtered log shows LSPosed loading `com.firestone.hooks`, `JNI_OnLoad firestonehooks`, `libil2cpp.so base=...`, the Easy-Win hooks installed, `easy-win FreeCurrency toggle active; affordability + no-spend hooks installed`, and `native hook install complete result=0`. No crash markers were present in the filtered launch log.

The current Free Currency toggle is not a balance grant. It makes affordability checks pass and prevents local pay/remove helpers from subtracting when those helpers are used. Big-number gold/firestone-style costs route through `BigCurrency.HaveCurrency(ObscuredBigDouble)`, which is now hooked with the correct pointer ABI. Verification on 2026-05-07 showed:

```text
EasyWin.BigCurrency.HaveCurrency hook installed rva=0x2ac2d98 ... OK
EasyWin.BigCurrency.RemoveCur hook installed rva=0x2ac30f8 ... OK
easy-win FreeCurrency BigCurrency.HaveCurrency hit #1...
```

A single tap on an otherwise unaffordable upgrade changed the upgrade panel state, confirming the BigCurrency affordability bypass is visible in-game. That tap did not emit a `RemoveCur no-spend` line, so that particular upgrade path appears to be gated mainly by `HaveCurrency` and then completed through a higher-level purchase/upgrade state update rather than directly calling `BigCurrency.RemoveCur`.

Anniversary coin-exchange costs use a separate path. `PremiumProductAnniversaryInteraction.PurchaseItem()`
sets `PremiumProduct.cost`, runs `AnniversaryEventExchangeCallback(code, quantity)`, then sends
`SocketFunctions.ExchangeCalendarCurrency(productCodeString, true, quantity)`. That socket call has
no client-side cost argument. The `Event exchange zero cost` toggle therefore zeros local UI/cost
state and affordability, while `Event exchange local only` is a disabled-by-default diagnostic that
suppresses the outgoing socket after the local callback. If zero-cost purchases still snap back with
the socket log present, the server is overriding the local state.

Verification on 2026-05-07 produced:

```text
artifacts/logcat_event_exchange_launch.txt
artifacts/logcat_event_exchange_after_wait.txt
artifacts/firestone_event_exchange_launch.png
artifacts/firestone_event_exchange_after_wait.png
```

Key lines:

```text
event exchange zero-cost UI #1 product=70 quantity=0
event exchange CanExchange forced true #1 product=70
event exchange PurchaseItem zero-cost entry #1 product=70 quantity=1 productCost=0
event exchange PremiumProduct.CanPurchase forced true #1 product=70 quantity=1
event exchange DataHandler.PayCurrency no-spend #1 product=70 quantity=1
event exchange socket ExchangeCalendarCurrency #1 ... anniversary=1 quantity=1 localOnly=0
```

## Troubleshooting

| Symptom | Likely cause | Action |
|---|---|---|
| Module not loaded | LSPosed module disabled or scope missing | Enable module, scope `com.HolydayStudios.Firestone`, force-stop game. |
| No `JNI_OnLoad firestonehooks` | Native library not loaded in target | Check `xposedmodule=true`, `META-INF/xposed/java_init.list`, and `TemplateConfig.NATIVE_LIBRARY_NAME`. |
| `libil2cpp.so base not found` | Native install ran before Unity loaded IL2CPP | Current worker waits up to 30 s. Re-check logs for late base resolution before changing code. |
| Launch crash after hook install | Risky combat hook ABI or same-page hook conflict | Disable in this order: `one_hit_kill`, `attack_speed`, `game_speed`, then retry. Re-enable one at a time. |
| Game closes after turning off the old in-app `Module enabled` switch | Older builds could persist `enabled=false`, leaving Firestone to launch without module/native logs and then exit on its own PlayCore path | Install the current APK, force-stop `com.firestone.hooks` and Firestone, then relaunch. The setting is normalized back to `true`; use LSPosed Manager for global disablement. |
| Game closes after disabling `com.firestone.hooks` in LSPosed/Vector | The module is not loaded at all; logcat shows no `FirestoneHooks` lines and Firestone exits after its own PlayCore update request | Re-enable the module and scope, then use per-feature toggles for a no-feature run. A globally disabled module cannot change Firestone startup behavior. |
| Anniversary coin exchange buys then immediately reverts | Event purchase path sends `SocketFunctions.ExchangeCalendarCurrency(product, true, quantity)` and the server resync rejects/restores state | Test with `Event exchange zero cost` on. If it still reverts and the socket log appears, enable `Event exchange local only` to confirm the server reply is the reset source; this local-only mode is not persistent server state. |
| Crash at `libil2cpp.so+0x2ac2dbc` | Wrong `BigCurrency.HaveCurrency` ABI or old function-entry return-skip build | Install the current build. It uses the observed ABI: `this` in `x0`, `ObscuredBigDouble*` in `x1`, `MethodInfo*` in `x2`. |
| Free currency is on but balance number does not increase | The toggle is an affordability/no-spend feature, not a currency grant | Try an otherwise unaffordable soft-currency upgrade and check for `BigCurrency.HaveCurrency hit` or normal `HaveCurrency hit` in logcat. A visible balance increase requires a separate reward/grant hook. |
| Crash only with OHK | `BattleController.ApplyDamage` ABI or target-side guard mismatch | Keep `one_hit_kill=false`; prefer god-mode/free-currency until dynamic register tracing confirms the call-site ABI. |
| Attack speed installs but feels unchanged | Only the stat interval path is active, or the hero was already inside the live attack loop | Enable `attack_speed_idle_timer` and `attack_speed_attack_timer`; these touch the live `CharacterBattleLogic` timers. `attack_speed_roster_stat` updates all ally hero `Hero.attackSpeedBasic` values when the roster stat recalculates. |
| `ObscuredCheatingDetected` appears | Direct `ObscuredFloat` field write detected | Keep `attack_speed=false` or route through the helper at `0x211761C`; current implementation already prefers that helper. |
| Shadowhook duplicate/same-page error | Another module hooked the same RVA | Disable the conflicting module or switch the affected hook to an intercept-only strategy. |
