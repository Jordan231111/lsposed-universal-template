'use strict';

const attached = new Set();
const classNames = new Map();

function ptrText(p) {
  try {
    return p && !p.isNull() ? p.toString() : '0x0';
  } catch (_) {
    return '<?>'
  }
}

function moduleOffset(p) {
  try {
    const m = Process.findModuleByAddress(p);
    if (!m) return ptrText(p);
    return `${m.name}+0x${p.sub(m.base).toString(16)}(${p})`;
  } catch (_) {
    return ptrText(p);
  }
}

function readCString(p) {
  try {
    if (!p || p.isNull()) return '';
    return p.readCString() || '';
  } catch (_) {
    return '';
  }
}

function attachExport(moduleName, symbol, callbacks) {
  let address = null;
  try {
    address = Module.findExportByName(moduleName, symbol);
  } catch (_) {}
  if (!address || address.isNull()) return false;
  const key = `${symbol}@${address}`;
  if (attached.has(key)) return true;
  attached.add(key);
  console.log(`[+] ${symbol} ${moduleOffset(address)}`);
  try {
    Interceptor.attach(address, callbacks);
    return true;
  } catch (e) {
    console.log(`[-] attach ${symbol} failed: ${e}`);
    return false;
  }
}

function attachDlopen() {
  ['android_dlopen_ext', 'dlopen'].forEach(name => {
    attachExport(null, name, {
      onEnter(args) {
        this.path = readCString(args[0]);
      },
      onLeave(retval) {
        if (!this.path) return;
        if (this.path.indexOf('bmt') >= 0 || this.path.indexOf('il2cpp') >= 0 ||
            this.path.indexOf('unity') >= 0 || this.path.indexOf('shadowhook') >= 0) {
          console.log(`[dlopen] ${this.path} -> ${ptrText(retval)}`);
          setTimeout(attachKnownHooks, 1);
        }
      }
    });
  });
}

function attachDlsym() {
  attachExport(null, 'dlsym', {
    onEnter(args) {
      this.name = readCString(args[1]);
    },
    onLeave(retval) {
      if (!this.name) return;
      if (this.name.indexOf('il2cpp_') === 0 || this.name.indexOf('Dobby') === 0) {
        console.log(`[dlsym] ${this.name} -> ${moduleOffset(retval)}`);
      }
    }
  });
}

function attachDobby() {
  attachExport(null, 'DobbyHook', {
    onEnter(args) {
      this.target = args[0];
      this.replacement = args[1];
      this.originalPtr = args[2];
      console.log(`[DobbyHook] target=${moduleOffset(args[0])} replacement=${moduleOffset(args[1])} originalPtr=${ptrText(args[2])}`);
    },
    onLeave(retval) {
      let original = 'unreadable';
      try {
        if (this.originalPtr && !this.originalPtr.isNull()) original = moduleOffset(this.originalPtr.readPointer());
      } catch (_) {}
      console.log(`[DobbyHook] retval=${retval.toInt32()} original=${original}`);
    }
  });

  attachExport(null, 'DobbyCodePatch', {
    onEnter(args) {
      console.log(`[DobbyCodePatch] address=${moduleOffset(args[0])} length=${args[2].toUInt32()}`);
    },
    onLeave(retval) {
      console.log(`[DobbyCodePatch] retval=${retval.toInt32()}`);
    }
  });
}

function attachIl2cpp() {
  attachExport('libil2cpp.so', 'il2cpp_class_from_name', {
    onEnter(args) {
      this.ns = readCString(args[1]);
      this.name = readCString(args[2]);
    },
    onLeave(retval) {
      if (retval.isNull()) return;
      const full = this.ns ? `${this.ns}.${this.name}` : this.name;
      classNames.set(retval.toString(), full);
      console.log(`[il2cpp_class_from_name] ${full} -> ${moduleOffset(retval)}`);
    }
  });

  attachExport('libil2cpp.so', 'il2cpp_class_get_method_from_name', {
    onEnter(args) {
      const klass = classNames.get(args[0].toString()) || ptrText(args[0]);
      this.line = `${klass}.${readCString(args[1])}/${args[2].toInt32()}`;
    },
    onLeave(retval) {
      console.log(`[il2cpp_class_get_method_from_name] ${this.line} -> ${moduleOffset(retval)}`);
    }
  });

  attachExport('libil2cpp.so', 'il2cpp_class_get_field_from_name', {
    onEnter(args) {
      const klass = classNames.get(args[0].toString()) || ptrText(args[0]);
      this.line = `${klass}.${readCString(args[1])}`;
    },
    onLeave(retval) {
      console.log(`[il2cpp_class_get_field_from_name] ${this.line} -> ${moduleOffset(retval)}`);
    }
  });
}

function attachKnownHooks() {
  attachDobby();
  attachIl2cpp();
}

console.log('[*] native minimal tracer loaded');
attachDlopen();
attachDlsym();
attachKnownHooks();
const timer = setInterval(attachKnownHooks, 100);
setTimeout(() => clearInterval(timer), 15000);

