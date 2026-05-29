# Rogue Recovery Notes

This clone is configured as a separate LSPosed module:

- Module package: `com.jordan.rogue.recovery`
- Target package: `net.room6.horizon`
- Recovered target Activity hook: `net.room6.horizon.MyActivity.onCreate(android.os.Bundle)`
- Native library in this clone: `librogue_recovery.so`

## Recovered Old Module Facts

The old APK/module used a legacy Xposed entry point:

- `assets/xposed_init`: `com.android.support.ModEntry`
- Scope: `net.room6.horizon`
- Java entry: `com.android.support.ModEntry`
- Native entry library loaded by Java: `libbmt.so`

The old Java path was:

1. Hook `net.room6.horizon.MyActivity.onCreate(Bundle)`.
2. After the original `onCreate` returns, call `Main.StartWithoutPermission(activity)`.
3. `Main.StartWithoutPermission` calls native `CheckOverlayPermission(context)` and then `StartWithContext(context)`.
4. Native code loads an embedded DEX through `dalvik.system.InMemoryDexClassLoader`.
5. Native code registers `com.android.support.Menu` methods and calls `Menu.CreateMenu(context)`.

Recovered native menu methods:

- `Icon`
- `IconWebViewData`
- `Init`
- `SettingsList`
- `GetFeatureList`
- `Changes`

Recovered feature mapping in `Changes(context, featureNum, ..., value, bool, ...)`:

- `featureNum == 0`: damage multiplier, minimum `1`
- `featureNum == 1`: defense multiplier, minimum `1`
- `featureNum == 2`: god mode boolean
- `featureNum == 3`: free shop boolean

Recovered menu items:

- `Category_MOD FEATUREs`
- `RichTextView_<b>READ ME :</b><br/>...`
- `InputValue_DAMAGE MULTI`
- `InputValue_DEFENSE MULTI`
- `ButtonOnOff_GOD MODE`
- `ButtonOnOff_FREE SHOP`
- `RichTextView_<b>Mod works on environments in which original apk works.</b>`
- `ButtonLink_REQUEST UPDATE_https://blackmod.net/`

## Current Port Status

Implemented in this clone:

- Static LSPosed scope for `net.room6.horizon`.
- Modern libxposed API 101 entry at `com.jordan.rogue.recovery.ModuleEntry`.
- Recovered Activity hook: `MyActivity.onCreate(Bundle)`.
- Overlay controls for the recovered feature state.
- Java-to-native feature sync for damage, defense, god mode, free shop, integrity repair,
  backup-result forge, ACTk bypass, and game speed. Persisted values are primed into native
  atomics before the installer waits on `libil2cpp.so`; Unity-facing speed changes still apply
  only from safe IL2CPP callbacks.
- Native IL2CPP bootstrap that waits for `libil2cpp.so`, resolves its ELF load base from
  `/proc/self/maps` plus the executable `PT_LOAD` program header, hooks `il2cpp_init`,
  `il2cpp_init_utf16`, and `il2cpp_runtime_invoke`, then resolves all recovered ShadowHook
  targets from IL2CPP metadata by assembly/class/method name after the runtime is initialized.
  Feature-critical managed fields (`speedUpValue`, `Fighter.param`, `Params.isPlayer/isEnemy`,
  `IntegrityTokenRequest.CloudProjectNumber`, and backup/verify closure fields) are also
  resolved by field name. No Rogue feature hook depends on a target method RVA.

Current native hooks for Rogue with the Dead `3.11.1` arm64:

- Damage multiplier: enemy-side `MonsterParams.get_damageTakenRate`, plus generic `Params.get_damageTakenRate`.
- Defense multiplier: player-side `PlayerParams.get_damageTakenRate`, `PlayerScaledParams.get_damageTakenRate`, `UniquePlayerParams.get_damageTakenRate`, plus generic `Params.get_damageTakenRate`.
- God mode: player-filtered `Fighter.DecreaseHp` and `Fighter.DecreaseHpWithoutSpGuard`.
- Free shop: `ShopMaster.CalcPrice`, `ShopMaster.GetPriceType`, `ShopMaster.get_IsIAP`, `UseCase_Purchase.CanPurchase`, `UseCase_ViewSeasonalShopMenu.CanPurchase`, `UseCase_GameEvent.CanPurchase`, `Utils.CheckIfIsEnough`, `Utils.Consume`, `SoldierData.CheckIfApIsEnough`, and `SoldierData.Consume`.
- Integrity bypass: `IntegrityTokenRequest..ctor` injects the app's Google cloud project number
  so the real `RequiredIntegrityCheck` token path can run, `RogueServerCode.get_IsSuccess`
  stays pass-through so a missing backup code is not hidden, read-only counters track the
  PrepareIntegrityCheck / RequestIntegrityTokenAsync state machines, and lower PlayFab transport
  hooks log the exact serialized `IssueBackupKey_*` request/response. See
  `docs/INTEGRITY_BYPASS_NOTES.md`.
- Backup-result forge: `IssueBackupKey` / `VerifyBackupKey` success and error closures can be
  rewritten to `RogueServerCode.Success` with a generated backup key or the user-entered PlayFab
  id when `forge_backup_success` is enabled.
- Game speed: `UseCase_SwitchGameSpeed.ReflectSpeedUp`, `UnityEngine.Time.set_timeScale`, and
  `UseCase_SwitchGameSpeed.SetupNewMode` keep a saved or live slider multiplier applied without
  calling Unity before IL2CPP is ready.

Runtime verification on the emulator:

- Old module `net.room6.horizon.com.android.support` is disabled because it crashes this app build while calling `il2cpp_domain_get`.
- New module `com.jordan.rogue.recovery` is enabled and scoped to `net.room6.horizon`.
- Debug APK installs and should log bootstrap hooks first, `IL2CPP metadata install starting after
  il2cpp_init`, resolved field names, and `installed 42 recovered hooks`.
- The overlay can briefly report `Native bootstrap waiting for IL2CPP` while the safe bootstrap is
  armed. It auto-refreshes to `Native recovered hooks installed` after the metadata hook pass
  succeeds.
- Frida attach through the patched 17.9.8 server successfully drove the module registry and logcat confirmed native state sync: `damage=5 defense=7 god=1 free_shop=1`.
- PAIRIP licensing now starts cleanly: `Hooked Application.attach`, `IntegrityBypass installed 6 java hooks`, `PAIRIP SignatureCheck.verifyIntegrity bypassed`, `PAIRIP initializeLicenseCheck bypassed`.
- Tapping Options > Cloud save > Save now keeps the game-owned integrity-token branch before
  `IssueBackupKey_MeWt8oWsFH`, so success is tied to the real PlayFab backup-code payload
  instead of a cosmetic `IsSuccess` override. The current MuMu test still returns
  `VerifyIntegrityVerdictUnevaluated(100034)` from `IssueBackupKey_MeWt8oWsFH`; the lower
  PlayFab trace shows non-empty `MessageHash` / `SignedToken` inputs and a server response with
  only `{"rogueReturnCode":100034}`, so the transfer code is absent from the server payload.
  See `docs/INTEGRITY_BYPASS_NOTES.md` for the full layered design (PAIRIP, Play Integrity,
  ACTk), rejected branch tests, and the device-side prep.

Current automated cloud-save smoke test from the loaded main game screen:

```bash
adb -s 127.0.0.1:16384 shell input tap 1135 110 && sleep 0.7 && adb -s 127.0.0.1:16384 shell input tap 1035 1315 && sleep 0.7 && adb -s 127.0.0.1:16384 shell input tap 1000 1535 && sleep 8 && adb -s 127.0.0.1:16384 exec-out screencap -p > /tmp/rwtd-screen.png
```

## Suggested Runtime Trace Flow

1. Enable the old working module/APK on a rooted test device with Frida server running.
2. Spawn the target app with hook tracing:

```bash
frida -U -f net.room6.horizon -l scripts/frida/trace_old_module_hooks.js
```

3. Open the old menu and toggle each recovered feature once.
4. Save the Frida output. The key lines are:

- `DobbyHook target=... replacement=...`
- `class_from_name ...`
- `class_get_method_from_name ...`

5. Port the resolved IL2CPP method names and hook addresses into `app/src/main/cpp/template_native.cpp`.
