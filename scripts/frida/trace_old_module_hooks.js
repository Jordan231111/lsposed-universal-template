'use strict';

const seen = new Set();
const classNames = new Map();

function once(key, fn) {
  if (seen.has(key)) return;
  seen.add(key);
  fn();
}

function readUtf8(value) {
  try {
    if (value === null || value.isNull()) return '';
    return value.readUtf8String() || '';
  } catch (_) {
    return '';
  }
}

function findExport(moduleName, symbol) {
  try {
    const p = Module.findExportByName(moduleName, symbol);
    if (p) return p;
  } catch (_) {}
  try {
    const dbg = DebugSymbol.fromName(symbol);
    if (dbg && dbg.address && !dbg.address.isNull()) return dbg.address;
  } catch (_) {}
  return null;
}

function describeAddress(address) {
  try {
    const m = Process.findModuleByAddress(address);
    if (!m) return address.toString();
    return `${m.name}+0x${address.sub(m.base).toString(16)} (${address})`;
  } catch (_) {
    return address.toString();
  }
}

function attachDobby() {
  const hook = findExport(null, 'DobbyHook') || findExport('libbmt.so', 'DobbyHook');
  if (hook) {
    once(`DobbyHook:${hook}`, () => {
      console.log(`[+] DobbyHook @ ${describeAddress(hook)}`);
      Interceptor.attach(hook, {
        onEnter(args) {
          this.target = args[0];
          this.replacement = args[1];
          this.origin = args[2];
          console.log(`[DobbyHook] target=${describeAddress(this.target)} replacement=${describeAddress(this.replacement)} originPtr=${this.origin}`);
          try {
            console.log(Thread.backtrace(this.context, Backtracer.ACCURATE).map(describeAddress).join('\n  '));
          } catch (_) {}
        },
        onLeave(retval) {
          let original = 'unreadable';
          try {
            if (!this.origin.isNull()) original = this.origin.readPointer().toString();
          } catch (_) {}
          console.log(`[DobbyHook] retval=${retval.toInt32()} original=${original}`);
        }
      });
    });
  }

  const patch = findExport(null, 'DobbyCodePatch') || findExport('libbmt.so', 'DobbyCodePatch');
  if (patch) {
    once(`DobbyCodePatch:${patch}`, () => {
      console.log(`[+] DobbyCodePatch @ ${describeAddress(patch)}`);
      Interceptor.attach(patch, {
        onEnter(args) {
          console.log(`[DobbyCodePatch] address=${describeAddress(args[0])} bytes=${args[1]} length=${args[2].toUInt32()}`);
        },
        onLeave(retval) {
          console.log(`[DobbyCodePatch] retval=${retval.toInt32()}`);
        }
      });
    });
  }
}

function attachIl2cpp() {
  const fromName = findExport('libil2cpp.so', 'il2cpp_class_from_name');
  if (fromName) {
    once(`class_from_name:${fromName}`, () => {
      console.log(`[+] il2cpp_class_from_name @ ${describeAddress(fromName)}`);
      Interceptor.attach(fromName, {
        onEnter(args) {
          this.ns = readUtf8(args[1]);
          this.name = readUtf8(args[2]);
        },
        onLeave(retval) {
          if (retval.isNull()) return;
          const full = this.ns ? `${this.ns}.${this.name}` : this.name;
          classNames.set(retval.toString(), full);
          console.log(`[il2cpp] class_from_name ${full} -> ${describeAddress(retval)}`);
        }
      });
    });
  }

  const methodFromName = findExport('libil2cpp.so', 'il2cpp_class_get_method_from_name');
  if (methodFromName) {
    once(`class_get_method:${methodFromName}`, () => {
      console.log(`[+] il2cpp_class_get_method_from_name @ ${describeAddress(methodFromName)}`);
      Interceptor.attach(methodFromName, {
        onEnter(args) {
          this.klass = args[0];
          this.method = readUtf8(args[1]);
          this.argc = args[2].toInt32();
        },
        onLeave(retval) {
          const klass = classNames.get(this.klass.toString()) || this.klass.toString();
          console.log(`[il2cpp] class_get_method_from_name ${klass}.${this.method}/${this.argc} -> ${describeAddress(retval)}`);
        }
      });
    });
  }
}

function attachMenuChanges() {
  if (!Java.available) return;
  Java.perform(() => {
    Java.enumerateClassLoaders({
      onMatch(loader) {
        try {
          const Menu = Java.ClassFactory.get(loader).use('com.android.support.Menu');
          once(`java-menu:${loader}`, () => {
            console.log('[+] com.android.support.Menu found in classloader');
            try {
              Menu.Changes.implementation = function (ctx, featureNum, featureName, value, boolValue, text) {
                console.log(`[Menu.Changes] feature=${featureNum} name=${featureName} value=${value} bool=${boolValue} text=${text}`);
                return this.Changes(ctx, featureNum, featureName, value, boolValue, text);
              };
            } catch (e) {
              console.log(`[-] could not hook Menu.Changes: ${e}`);
            }
            try {
              Menu.GetFeatureList.implementation = function () {
                const result = this.GetFeatureList();
                console.log(`[Menu.GetFeatureList] count=${result ? result.length : 0}`);
                if (result) {
                  for (let i = 0; i < result.length; i += 1) console.log(`  [${i}] ${result[i]}`);
                }
                return result;
              };
            } catch (e) {
              console.log(`[-] could not hook Menu.GetFeatureList: ${e}`);
            }
          });
        } catch (_) {}
      },
      onComplete() {}
    });
  });
}

console.log('[*] tracing Dobby, IL2CPP method resolution, and old Menu.Changes');
setInterval(() => {
  attachDobby();
  attachIl2cpp();
  attachMenuChanges();
}, 500);

