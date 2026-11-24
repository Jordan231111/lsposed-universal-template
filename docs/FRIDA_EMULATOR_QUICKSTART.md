# Frida reconnaissance

This example attaches to a running app on an authorized test device using Frida 17 and a
matching Android server. For a TCP emulator, connect ADB to the current address before
selecting the device. MuMu can assign a new ADB port after a restart.

From the repository root, select the Frida device ID and the running app's package identifier.

```sh
frida-ls-devices
frida-ps -D DEVICE_ID -a
frida -D DEVICE_ID -N com.example.target --runtime=qjs -l scripts/frida/android_recon.js
```

Replace `DEVICE_ID` and `com.example.target` with the values from the listings. Use `-p PID`
for a particular process or `-n` for a process name. The agent is read-only by default.

```javascript
recon.modules()
recon.exports('libtarget.so')
recon.classes('YourNamespace|YourClass', 100)
recon.watchLoads()             // Start observing library loads
recon.stopWatchingLoads()
```

RPC names are `modules`, `exports`, `classes`, `watchloads` and `stopwatchingloads`.
Class queries require the Java bridge. The Frida REPL and `frida-trace` include it.
Standalone agents that query Java must supply a bridge compatible with the device.
[Frida bridge documentation](https://frida.re/docs/bridges/)
