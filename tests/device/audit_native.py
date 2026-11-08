#!/usr/bin/env python3
"""Build and run an owned delayed-load/native-hook fixture; touches no game package."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import tempfile
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--device", required=True)
    p.add_argument("--delay", type=int, default=0)
    p.add_argument("--abi", choices=("arm64-v8a", "armeabi-v7a"), default="arm64-v8a")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--il2cpp-observer", action="store_true", help="Exercise readiness/shutdown using an owned synthetic runtime")
    args = p.parse_args()
    if not 0 <= args.delay <= 300:
        raise ValueError("Delay must be between 0 and 300 seconds")
    sdk = Path(os.environ.get("ANDROID_HOME", Path.home() / "Library/Android/sdk"))
    ndk = re.search(r'ndkVersion\s*=\s*"([^"]+)"', (ROOT / "app/build.gradle.kts").read_text())[1]
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    triple = "aarch64-linux-android28" if args.abi == "arm64-v8a" else "armv7a-linux-androideabi28"
    compiler = sdk / "ndk" / ndk / "toolchains/llvm/prebuilt" / host / "bin" / (triple + "-clang++")
    aar = next((Path.home() / ".gradle/caches/modules-2/files-2.1/com.bytedance.android/shadowhook/2.0.1").rglob("*.aar"))
    remote = "/data/local/tmp/template-native-" + uuid.uuid4().hex[:12]

    def adb(*parts, **kwargs):
        result = subprocess.run(["adb", "-s", args.device, *map(str, parts)], capture_output=True, text=True, **kwargs)
        if result.returncode:
            raise RuntimeError(f"ADB failed ({result.returncode}): {result.stdout}{result.stderr}")
        return result

    try:
        with tempfile.TemporaryDirectory(prefix="template-native-") as folder:
            work = Path(folder)
            with zipfile.ZipFile(aar) as archive:
                (work / "shadowhook.h").write_bytes(archive.read("prefab/modules/shadowhook/include/shadowhook.h"))
                (work / "libshadowhook.so").write_bytes(archive.read(f"jni/{args.abi}/libshadowhook.so"))
                (work / "libshadowhook_nothing.so").write_bytes(archive.read(f"jni/{args.abi}/libshadowhook_nothing.so"))
            common = [str(compiler), "-std=c++20", "-fexceptions", "-static-libstdc++", "-ffunction-sections",
                      "-fdata-sections", "-Wl,--gc-sections", "-I", str(ROOT / "app/src/main/cpp"), "-I", str(work)]
            library_source = ROOT / "tests/device/late_library.cpp"
            probe_source = ROOT / "tests/device/native_probe.cpp"
            extra_sources = []
            if args.il2cpp_observer:
                common += ["-I", str(ROOT / "tests/cpp")]
                library_source = work / "fake_exports.cpp"
                code = '#include "il2cpp_fixture.h"\nstatic fixture::Fake fake;\n'
                header = (ROOT / "app/src/main/cpp/il2cpp_runtime.h").read_text()
                for returns, name, arguments in re.findall(r'X\(([^,]+), (\w+), \((.*?)\)\)', header):
                    types = arguments.split(", ") if arguments else []
                    named = ", ".join(f"{kind} a{index}" for index, kind in enumerate(types))
                    forwarded = ", ".join(f"a{index}" for index in range(len(types)))
                    guard = 'if (!a0) return nullptr;' if name == "runtime_invoke" else ""
                    code += f'extern "C" {returns} il2cpp_{name}({named}) {{ {guard} return fake.api.{name}({forwarded}); }}\n'
                code += 'extern "C" void il2cpp_shutdown() {}\n'
                library_source.write_text(code)
                probe_source = ROOT / "tests/device/il2cpp_observer_probe.cpp"
                extra_sources = [str(ROOT / "app/src/main/cpp/il2cpp_runtime.cpp"), str(ROOT / "app/src/main/cpp/il2cpp_observer.cpp")]
            subprocess.run(common + ["-shared", "-fPIC", str(library_source),
                                     "-o", str(work / "libtemplate_late.so")], check=True, capture_output=True)
            subprocess.run(common + [str(probe_source), *extra_sources,
                            str(ROOT / "app/src/main/cpp/library_observer.cpp"), str(ROOT / "app/src/main/cpp/native_utils.cpp"),
                            "-L", str(work), "-lshadowhook", "-llog", "-ldl", "-o", str(work / "native-probe")],
                           check=True, capture_output=True)
            adb("shell", "mkdir", "-p", remote)
            for name in ("native-probe", "libshadowhook.so", "libshadowhook_nothing.so", "libtemplate_late.so"):
                adb("push", work / name, remote + "/" + name)
            adb("shell", "chmod", "755", remote + "/native-probe")
            command = f"LD_LIBRARY_PATH={shlex.quote(remote)} {shlex.quote(remote + '/native-probe')} {shlex.quote(remote + '/libtemplate_late.so')} {args.delay}"
            result = adb("shell", command, timeout=args.delay + 45)
            receipt = {"abi": args.abi, "fixture": "il2cpp-observer" if args.il2cpp_observer else "native", "result": json.loads(result.stdout.strip())}
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(receipt, indent=2) + "\n")
            print(json.dumps(receipt, indent=2))
    finally:
        adb("shell", "rm", "-rf", remote)


if __name__ == "__main__":
    main()
