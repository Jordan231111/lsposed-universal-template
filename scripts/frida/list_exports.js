'use strict';

// Use `libbmt.so` while tracing the recovered old module, or `libil2cpp.so`
// when inspecting the target game's IL2CPP exports.
const LIB_NAME = 'libbmt.so';

const module = Process.findModuleByName(LIB_NAME);
if (!module) {
  console.log('[exports] ' + LIB_NAME + ' is not loaded. Loaded app-ish modules:');
  Process.enumerateModules()
    .filter(function (m) {
      const n = m.name.toLowerCase();
      return n.indexOf('target') >= 0 || n.indexOf('game') >= 0 || n.indexOf('unity') >= 0 || n.indexOf('il2cpp') >= 0;
    })
    .forEach(function (m) { console.log('  ' + m.name + ' ' + m.path); });
} else {
  console.log('[exports] ' + module.name + ' base=' + module.base + ' path=' + module.path);
  Module.enumerateExports(module.name)
    .sort(function (a, b) { return a.name.localeCompare(b.name); })
    .forEach(function (e) {
      console.log(e.type + ' ' + e.name + ' ' + e.address);
    });
}
