#!/usr/bin/env python3
"""Run the shipped C++ IL2CPP helper against framework-only operations in an owned test process.

Requires a matching running Frida server. Does not launch/stop apps, install gameplay hooks,
modify features, or invoke game methods. Its temporary library is removed after detaching.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shlex
import subprocess
import tempfile
import uuid
import zipfile

import frida

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", required=True)
    parser.add_argument("--package", required=True)
    parser.add_argument("--bridge", required=True, type=Path)
    parser.add_argument("--library", default="libil2cpp.so")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--bind-only", action="store_true", help="Validate export binding without metadata or managed calls")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)+", args.package):
        raise ValueError("Invalid package identity")

    def adb(*parts):
        return subprocess.run(["adb", "-s", args.device, *parts], check=True, capture_output=True).stdout.decode().strip()

    pids = adb("shell", "pidof", args.package).split()
    if len(pids) != 1:
        raise ValueError("Start one main test process before attaching")
    device = frida.get_device(args.device, timeout=10)
    session = device.attach(int(pids[0]))
    describe = session.create_script("rpc.exports.describe=()=>({arch:Process.arch,pid:Process.id});")
    describe.load()
    target = describe.exports_sync.describe()
    describe.unload()
    compiler_name = {"arm64": "aarch64-linux-android28-clang++", "arm": "armv7a-linux-androideabi28-clang++"}.get(target["arch"])
    if compiler_name is None:
        session.detach()
        raise ValueError("Native validation supports ARM/ARM64")
    sdk = Path(os.environ.get("ANDROID_HOME", Path.home() / "Library/Android/sdk"))
    ndk = re.search(r'ndkVersion\s*=\s*"([^"]+)"', (ROOT / "app/build.gradle.kts").read_text())[1]
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    compiler = sdk / "ndk" / ndk / "toolchains/llvm/prebuilt" / host / "bin" / compiler_name
    tag = pids[0] + "_" + uuid.uuid4().hex[:12]
    destination = f"/data/user/0/{args.package}/code_cache/libtemplate_audit_{tag}.so"
    transfer = f"/data/local/tmp/libtemplate_audit_{tag}.so"
    script = None
    try:
        with tempfile.TemporaryDirectory(prefix="template-il2cpp-") as folder:
            library = Path(folder) / "libaudit.so"
            aar = next((Path.home() / ".gradle/caches/modules-2/files-2.1/com.bytedance.android/shadowhook/2.0.1").rglob("*.aar"))
            with zipfile.ZipFile(aar) as archive:
                header = archive.read("prefab/modules/shadowhook/include/shadowhook.h")
                (Path(folder) / "shadowhook.h").write_bytes(header)
            subprocess.run([str(compiler), "-std=c++20", "-fPIC", "-shared", "-fexceptions",
                            "-static-libstdc++", "-fvisibility=hidden", "-ffunction-sections", "-fdata-sections",
                            "-Wl,--exclude-libs,ALL", "-Wl,--gc-sections", "-I", folder,
                            "-I", str(ROOT / "app/src/main/cpp"), str(ROOT / "tests/device/il2cpp_probe.cpp"),
                            str(ROOT / "app/src/main/cpp/il2cpp_runtime.cpp"),
                            str(ROOT / "app/src/main/cpp/native_utils.cpp"), "-llog", "-ldl", "-o", str(library)],
                           check=True, capture_output=True)
            adb("push", str(library), transfer)
            uid = adb("shell", "su", "-c", shlex.quote(f"stat -c %u /data/user/0/{args.package}"))
            if not uid.isdigit():
                raise ValueError("Cannot identify target UID")
            command = " && ".join((f"cp {shlex.quote(transfer)} {shlex.quote(destination)}",
                f"chown {uid}:{uid} {shlex.quote(destination)}", f"chmod 500 {shlex.quote(destination)}",
                f"restorecon {shlex.quote(destination)}"))
            adb("shell", "su", "-c", shlex.quote(command))
            names = re.findall(r"X\([^,]+,\s*(\w+),", (ROOT / "app/src/main/cpp/il2cpp_runtime.h").read_text())
            source = args.bridge.read_text() + "\nconst auditApiNames=" + json.dumps(["il2cpp_" + n for n in names]) + ";\n" + r'''
rpc.exports.audit = (path, library, initialized) => Il2Cpp.perform(() => {
  send({stage:'attached-worker'});
  const engine = Process.getModuleByName(library);
  const table = Memory.alloc(auditApiNames.length * Process.pointerSize);
  auditApiNames.forEach((name,i)=>table.add(i*Process.pointerSize).writePointer(engine.findExportByName(name) || ptr(0)));
  send({stage:'api-table-ready'});
  const dl = Process.getModuleByName('libdl.so');
  const open = new NativeFunction(dl.getExportByName('dlopen'),'pointer',['pointer','int']);
  const symbol = new NativeFunction(dl.getExportByName('dlsym'),'pointer',['pointer','pointer']);
  const close = new NativeFunction(dl.getExportByName('dlclose'),'int',['pointer']);
  const handle = open(Memory.allocUtf8String(path),2);
  if(handle.isNull())throw Error('The platform loader refused the QA library');
  try {
  send({stage:'probe-loaded'});
  const entry = symbol(handle,Memory.allocUtf8String('template_runtime_audit'));
  if(entry.isNull())throw Error('The platform loader could not resolve the QA entry point');
  send({stage:'entry-resolved'});
  const audit = new NativeFunction(entry, 'pointer',
      ['pointer', Process.pointerSize === 8 ? 'ulong' : 'uint', 'uchar'], {exceptions:'propagate'});
  send({stage:'calling-native', initialized});
  const result = audit(table, auditApiNames.length, initialized ? 1 : 0);
  return JSON.parse(result.readUtf8String());
  } finally { close(handle); }
});
'''
            script = session.create_script(source)
            script.on("message", lambda message, data: print(json.dumps(message), flush=True))
            script.load()
            result = script.exports_sync.audit(destination, args.library, not args.bind_only)
            receipt = {"package": args.package, "pid": int(pids[0]), "architecture": target["arch"],
                       "frida": frida.__version__, "result": result}
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(receipt, indent=2) + "\n")
            print(json.dumps(receipt, indent=2))
            if "error" in result:
                raise RuntimeError("Read-only runtime validation failed")
    finally:
        try:
            if script:
                try:
                    script.unload()
                except frida.InvalidOperationError:
                    pass
        finally:
            try:
                session.detach()
            finally:
                adb("shell", "su", "-c", shlex.quote(f"rm -f {shlex.quote(destination)} {shlex.quote(transfer)}"))


if __name__ == "__main__":
    main()
