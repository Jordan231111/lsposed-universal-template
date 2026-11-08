# Frida reconnaissance

Use a matching host/server/bridge set on an owned or authorized test process. Do not upgrade the
host with an unbounded pip command while leaving an older server or locally patched emulator kit.
The local default is the patched undetected-frida 17.25.0 server/gadget/inject kit with matching
17.25.0 Python/client and frida-tools 14.11.0. A newer upstream release does not replace this
validated pairing automatically. Updating the kit requires revalidating its local patches.

Discover the device instead of retaining a previous emulator port:

```sh
"/Applications/MuMuPlayer Pro.app/Contents/MacOS/mumu-cli" info 0
adb devices -l
frida --version
# Select that device explicitly for all following commands.
frida-ps -D DEVICE_SERIAL -a
```

Match the server's architecture to the actual process/device. Native template helpers support
ARM/ARM64. Root/writable-system remounts are not required simply to read metadata through an
already configured test server. Do not change root-manager or SELinux settings as a routine step.

## One maintained agent

```sh
frida -D DEVICE_SERIAL -N com.example.target -l scripts/frida/android_recon.js
# Or attach by a confirmed PID:
frida -D DEVICE_SERIAL -p PID -l scripts/frida/android_recon.js
```

`-N` selects an application identifier; `-n` selects a process name and may not equal the package.
The agent is read-only by default and installs no Activity or Runtime.loadLibrary hooks.
In the Frida REPL:

```javascript
recon.modules()
recon.exports('libtarget.so')
recon.classes('YourNamespace|YourClass', 100)
recon.watchLoads()             // optional loader observation
recon.stopWatchingLoads()
```

RPC equivalents are `modules`, `exports`, `classes`, `watchloads`, `stopwatchingloads`.
A custom Python/Gadget agent must bundle `frida-java-bridge` for Java queries; the Frida CLI
supplies it. Native reconnaissance remains usable without Java. Class query output is bounded.

Frida 17 removed static `Module.findExportByName` and `Module.enumerateExports`. The agent uses
module instances and searches exports through their owning modules, which also avoids assuming
that every emulator exposes a symbol through the global lookup namespace. See the
[Frida 17 migration notes](https://frida.re/news/2025/05/17/frida-17-0-0-released/).

## Keep observations separate from release behavior

Record process/ABI, library identity, actual call signature, readiness and lifetime before making
an implementation. Use metadata and bounded native analysis together. A successful attachment,
non-null function pointer or completed network callback does not establish feature correctness.

External C++ helper validation lives under `tests/device/`, never in the APK. It runs framework-only
operations and preserves target settings. Its QA library uses the platform loader/export lookup;
record instrumentation failures separately from production-helper results. See
[validation](VALIDATION.md). No game purchases, reward grants or gameplay methods are part of the
runtime compatibility probe.
