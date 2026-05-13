# Game Modules - Overview

Two production LSPosed modules ship alongside the base template:

| Module | Target package | App | Doc |
| --- | --- | --- | --- |
| `modules/rogue/` | `net.room6.horizon` | Rogue with the Dead (room6, v3.11.1) | [`MODULE_ROGUE.md`](MODULE_ROGUE.md) |
| `modules/once/` | `work.ponix.onceworld` | OnceWorld (Ponix, v2.2.5) | [`MODULE_ONCE.md`](MODULE_ONCE.md) |

Both modules:

- Are **Java-only** LSPosed modules - the produced APKs contain no native code.
- Are **ABI-neutral** - one APK loads on both `arm64-v8a` devices **and** `x86_64` emulators.
- Target the **libxposed 101.0.1** API (`io.github.libxposed:api:101.0.1`).
- Live in their own Gradle subproject; the base `:app` template is untouched.
- Share an identical hook surface because both games share the same anti-cheat stack
  (**PAIRIP** + **ACTk**) and the same engine (**Unity IL2CPP**).

## Build everything at once

```bash
./gradlew :modules:rogue:assembleRelease :modules:once:assembleRelease
```

APK outputs:

- `modules/rogue/build/outputs/apk/release/rogue-release.apk`
- `modules/once/build/outputs/apk/release/once-release.apk`

## Design Notes

### Why Java-only

Both target apps ship only `arm64-v8a` native splits. A native ShadowHook module:

- works on physical arm64 devices,
- **does not** work on Intel x86_64 emulators (ShadowHook supports arm/arm64 only, and even
  if it did the game runs under ARM translation),
- breaks every time the IL2CPP method-pointer table shifts.

A Java module that hooks `System.currentTimeMillis`, the ACTk Java bridge, and PAIRIP's Java
entry points achieves the same gameplay-level acceleration without any of those caveats.

### Why time-acceleration covers most "cheat" features

Idle / incremental games are **fundamentally time-gated**. By increasing the rate at which the
game advances, every cooldown collapses, every offline-tick reward scales up, and every
"X / hour" mechanic accelerates. We achieve the same observable effect as currency edits,
XP multipliers, or cooldown skips - without ever touching IL2CPP state directly.

### Why direct IL2CPP edits are deliberately omitted

Direct IL2CPP edits (write to a `gold: long` field, override a `Damage` method) require:

1. resolving IL2CPP class-info and method-info pointers via the loaded library base + offsets,
2. those offsets changing on every Unity recompile,
3. on x86_64 emulators, the offsets do not even exist because the game runs under translation.

The chosen Java hooks have **no such dependency**: they hook framework-supplied methods whose
signatures have been stable since Android 8. That is what "version resilience" means in
practice.
