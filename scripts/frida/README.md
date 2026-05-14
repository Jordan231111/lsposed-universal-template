# Frida Runtime Tracing

Use these scripts on a rooted test device with Frida server running.

The current emulator is using `zer0def/undetected-frida` 17.9.8 for Android arm64:

- server: `/data/local/tmp/.ufd64`
- gadget: `/data/local/tmp/.ufg64.so`
- macOS Python client: `frida==17.9.8`

Trace the old working module to recover exact IL2CPP hook targets:

```bash
frida -U -f net.room6.horizon -l scripts/frida/trace_old_module_hooks.js
```

Then open the old menu and toggle each feature once. Keep the lines containing:

- `DobbyHook`
- `DobbyCodePatch`
- `class_from_name`
- `class_get_method_from_name`
- `Menu.Changes`

Those lines identify which IL2CPP classes/methods the old native library resolved and which native addresses it passed into Dobby.

If spawn mode misses early calls, attach after manually launching the app:

```bash
frida -U -n net.room6.horizon -l scripts/frida/trace_old_module_hooks.js
```

To verify that the recovered LSPosed module's menu state reaches native hooks, attach by PID after launch:

```bash
pid=$(adb shell pidof net.room6.horizon | tr -d '\r')
frida -U -p "$pid" -l scripts/frida/set_recovered_features_once.js
```

Expected logcat line after the script runs:

```text
recovered feature state: damage=5 defense=7 god=1 free_shop=1
```
