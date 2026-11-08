'use strict';

// Read-only by default. Frida's CLI supplies Java; custom agents must bundle the Java bridge.
// Use recon.modules(), recon.exports('libname.so'), recon.classes('regex') from the CLI REPL.
// Loader observation is explicit: recon.watchLoads(); recon.stopWatchingLoads().
globalThis.recon = (() => {
  const listeners = [];
  const moduleRows = () => Process.enumerateModules().map(m => ({
    name: m.name, base: m.base.toString(), size: m.size, path: m.path
  }));
  function findExport(name) {
    for (const module of Process.enumerateModules()) {
      const address = module.findExportByName(name);
      if (address !== null) return address;
    }
    return null;
  }
  function stopWatchingLoads() {
    let failure;
    for (let index = listeners.length - 1; index >= 0; --index) {
      try { listeners[index].detach(); listeners.splice(index, 1); }
      catch (error) { failure = error; }
    }
    if (failure) throw failure; // Keep ownership of any listener that could not be detached.
  }
  return {
    modules: moduleRows,
    exports(name) {
      const module = Process.findModuleByName(name);
      if (!module) throw Error('Module is not loaded: ' + name);
      return module.enumerateExports().map(e => ({
        type: e.type, name: e.name, address: e.address.toString()
      }));
    },
    classes(pattern, limit = 100) {
      if (typeof Java === 'undefined' || !Java.available)
        return Promise.reject(Error('Java bridge unavailable; use the Frida CLI or bundle frida-java-bridge'));
      const regex = new RegExp(pattern, 'i');
      const cap = Math.max(1, Math.min(1000, Math.trunc(Number(limit)) || 100));
      return new Promise((resolve, reject) => Java.perform(() => {
        try {
          const matches = [];
          let count = 0;
          Java.enumerateLoadedClasses({
            onMatch(name) {
              if (!regex.test(name)) return;
              ++count;
              if (matches.length < cap) matches.push(name);
            },
            onComplete() { resolve({matches, total: count, truncated: count > matches.length}); }
          });
        } catch (error) { reject(error); }
      }));
    },
    watchLoads() {
      if (listeners.length) return listeners.length;
      const seen = new Set();
      try {
        for (const name of ['android_dlopen_ext', 'dlopen']) {
          const address = findExport(name);
          if (address === null || seen.has(address.toString())) continue;
          seen.add(address.toString());
          listeners.push(Interceptor.attach(address, {
            onEnter(args) {
              try { this.path = args[0].isNull() ? null : args[0].readCString(); }
              catch (_) { this.path = '<unreadable>'; }
            },
            onLeave(result) { send({kind: 'library-load', loader: name, path: this.path, loaded: !result.isNull()}); }
          }));
        }
      } catch (error) { stopWatchingLoads(); throw error; }
      return listeners.length;
    },
    stopWatchingLoads,
  };
})();

rpc.exports = {
  modules: recon.modules,
  exports: recon.exports,
  classes: recon.classes,
  watchloads: recon.watchLoads,
  stopwatchingloads: recon.stopWatchingLoads,
};
console.log('[recon] pid=' + Process.id + ' arch=' + Process.arch + '; no hooks installed');
