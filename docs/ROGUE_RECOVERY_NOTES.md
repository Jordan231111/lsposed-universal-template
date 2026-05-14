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
- Java-to-native feature sync for damage, defense, god mode, and free shop.
- Native IL2CPP bootstrap that waits for `libil2cpp.so`, maps its load base from `/proc/self/maps`, and installs fixed-RVA ShadowHook hooks. It avoids calling exported IL2CPP API symbols because the old module crashes on the current app at the `il2cpp_domain_get` export path.

Current native hooks for Rogue with the Dead `3.11.1` arm64:

- Damage multiplier: enemy-side `MonsterParams.get_damageTakenRate`, plus generic `Params.get_damageTakenRate`.
- Defense multiplier: player-side `PlayerParams.get_damageTakenRate`, `PlayerScaledParams.get_damageTakenRate`, `UniquePlayerParams.get_damageTakenRate`, plus generic `Params.get_damageTakenRate`.
- God mode: player-filtered `Fighter.DecreaseHp` and `Fighter.DecreaseHpWithoutSpGuard`.
- Free shop: `ShopMaster.CalcPrice`, `ShopMaster.GetPriceType`, `ShopMaster.get_IsIAP`, `UseCase_Purchase.CanPurchase`, `UseCase_ViewSeasonalShopMenu.CanPurchase`, `UseCase_GameEvent.CanPurchase`, `Utils.CheckIfIsEnough`, `Utils.Consume`, `SoldierData.CheckIfApIsEnough`, and `SoldierData.Consume`.

Runtime verification on the emulator:

- Old module `net.room6.horizon.com.android.support` is disabled because it crashes this app build while calling `il2cpp_domain_get`.
- New module `com.jordan.rogue.recovery` is enabled and scoped to `net.room6.horizon`.
- Debug APK installs and logs `installed 17 recovered hooks`.
- Frida attach through the patched 17.9.8 server successfully drove the module registry and logcat confirmed native state sync: `damage=5 defense=7 god=1 free_shop=1`.
- Full gameplay validation is currently blocked on this emulator by PairIP licensing: `LicenseCheckException: Could not bind with the licensing service`.

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
