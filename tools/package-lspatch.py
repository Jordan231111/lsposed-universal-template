#!/usr/bin/env python3
"""Verify a modern module and produce a signed, aligned SAI split bundle atomically."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]

# Keep signing passwords in the environment, not Java's command line or generated source.
PATCH_RUNNER = r'''
import java.net.*;
import java.nio.file.*;
import java.util.*;
import java.util.jar.*;
class PrivatePatcher {
  public static void main(String[] args) throws Exception {
    var jar = Path.of(args[0]);
    var options = new ArrayList<String>();
    options.addAll(List.of("-k", System.getenv("TEMPLATE_KS_PATH"),
      System.getenv("TEMPLATE_KS_PASS"), System.getenv("TEMPLATE_KEY_ALIAS"),
      System.getenv("TEMPLATE_KEY_PASS")));
    options.addAll(Arrays.asList(args).subList(1, args.length));
    String main;
    try (var file = new JarFile(jar.toFile())) {
      main = file.getManifest().getMainAttributes().getValue("Main-Class");
    }
    try (var loader = new URLClassLoader(new URL[]{jar.toUri().toURL()}, ClassLoader.getPlatformClassLoader())) {
      Thread.currentThread().setContextClassLoader(loader);
      loader.loadClass(main).getMethod("main", String[].class).invoke(null, (Object) options.toArray(String[]::new));
    }
  }
}
'''


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def run(*arguments, env=None):
    result = subprocess.run([str(arg) for arg in arguments], env=env, capture_output=True, text=True)
    if result.returncode:
        message = (result.stderr or result.stdout)[-2000:]
        for key in ("TEMPLATE_KS_PASS", "TEMPLATE_KEY_PASS"):
            secret = (env or os.environ).get(key)
            if secret:
                message = message.replace(secret, "<redacted>")
        raise RuntimeError(f"{Path(arguments[0]).name} failed ({result.returncode}): {message}")
    return result.stdout


def signer(path):
    output = run("apksigner", "verify", "--min-sdk-version", "28", "--verbose", "--print-certs", path)
    certificates = set(re.findall(r"certificate SHA-256 digest: ([0-9a-f]{64})", output))
    if len(certificates) != 1:
        raise ValueError(f"Expected one signing certificate: {Path(path).name}")
    return certificates.pop()


def apk_identity(path):
    output = run("aapt2", "dump", "badging", path)
    package = next(line for line in output.splitlines() if line.startswith("package:"))
    fields = dict(re.findall(r"([A-Za-z]+)='([^']*)'", package))
    return fields["name"], fields["versionCode"], fields.get("split", "")


def validate_identities(identities):
    if not identities or len({(name, version) for name, version, _ in identities}) != 1:
        raise ValueError("Every original APK must have the same package and versionCode")
    splits = [split for _, _, split in identities]
    if splits.count("") != 1 or len(set(splits)) != len(splits):
        raise ValueError("Require exactly one base and unique split IDs")
    if any(split and not re.fullmatch(r"[A-Za-z0-9_.-]+", split) for split in splits):
        raise ValueError("Invalid split identity")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--receipt", type=Path)
    parser.add_argument("--local-test-signing", action="store_true", help="Explicitly use the existing local Android test key")
    parser.add_argument("apks", nargs="+", type=Path, help="Original unpatched base and every required split")
    args = parser.parse_args()
    module, output = args.module.resolve(), args.output.resolve()
    inputs = [path.resolve() for path in args.apks]
    env = os.environ.copy()
    signing_keys = ("TEMPLATE_KS_PATH", "TEMPLATE_KS_PASS", "TEMPLATE_KEY_ALIAS", "TEMPLATE_KEY_PASS", "TEMPLATE_CERT_SHA256")
    if args.local_test_signing:
        if any(env.get(key) for key in signing_keys):
            raise ValueError("Do not combine local test signing with release-signing variables")
        env.update(TEMPLATE_KS_PATH=str(Path.home() / ".android/debug.keystore"),
                   TEMPLATE_KS_PASS="android", TEMPLATE_KEY_ALIAS="androiddebugkey", TEMPLATE_KEY_PASS="android")
    elif any(not env.get(key) for key in signing_keys):
        raise ValueError("Production packaging requires all five TEMPLATE_* signing variables; see docs/LSPATCH_NONROOT.md")
    key = Path(env["TEMPLATE_KS_PATH"])
    if not key.is_absolute():
        raise ValueError("TEMPLATE_KS_PATH must be absolute")
    for path in (module, key, *inputs):
        if not path.is_file():
            raise ValueError(f"Required input missing: {path}")
    if output in (module, key, *inputs):
        raise ValueError("The output must not overwrite an input or signing key")
    if args.receipt and args.receipt.resolve() in (module, key, output, *inputs):
        raise ValueError("The receipt must not overwrite an input, signing key or bundle")
    cert = subprocess.run(["keytool", "-exportcert", "-keystore", str(key), "-storepass:env",
                           "TEMPLATE_KS_PASS", "-alias", env["TEMPLATE_KEY_ALIAS"]],
                          env=env, capture_output=True)
    if cert.returncode:
        raise ValueError("Could not read the configured signing certificate")
    certificate = hashlib.sha256(cert.stdout).hexdigest()
    expected = env.get("TEMPLATE_CERT_SHA256", certificate).strip().replace(":", "").lower()
    if not re.fullmatch(r"[0-9a-f]{64}", expected) or certificate != expected:
        raise ValueError("Signing key does not match TEMPLATE_CERT_SHA256")
    if signer(module) != expected:
        raise ValueError("Module does not use the configured signing identity")
    run("zipalign", "-c", "-P", "16", "4", module)
    identities = [apk_identity(path) for path in inputs]
    validate_identities(identities)
    if len({signer(path) for path in inputs}) != 1:
        raise ValueError("Original base and splits must share one verified signing identity")
    target_package = identities[0][0]
    if apk_identity(module)[0] == target_package:
        raise ValueError("Module and target must be different packages")
    with zipfile.ZipFile(module) as archive:
        entry = archive.read("META-INF/xposed/java_init.list").decode().strip()
        properties = archive.read("META-INF/xposed/module.prop").decode()
        scope = archive.read("META-INF/xposed/scope.list").decode().splitlines()
        if not entry or not re.search(r"^minApiVersion\s*=\s*102\s*$", properties, re.M) or target_package not in map(str.strip, scope):
            raise ValueError("Module must declare API 102, a modern entry point and this target's scope")
    for path in inputs:
        with zipfile.ZipFile(path) as archive:
            if "assets/lspatch/config.json" in archive.namelist():
                raise ValueError("Use original APKs, not a previously patched target")
    jar = Path(run(ROOT / "tools/download-lspatch.sh").strip())
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="lspatch-", dir=output.parent) as folder:
        stage = Path(folder)
        prepared = stage / "inputs"
        prepared.mkdir()
        originals = {}
        for path, (_, _, split) in zip(inputs, identities):
            destination = prepared / ("split_" + split + ".apk" if split else "base.apk")
            shutil.copyfile(path, destination)
            originals[split] = destination
        helper = stage / "PrivatePatcher.java"
        helper.write_text(PATCH_RUNNER)
        ordered = [originals[""]] + [originals[split] for split in sorted(originals) if split]
        run("java", helper, jar, "-m", module, "-o", stage, "-l", "2", *ordered, env=env)
        patched = sorted(stage.glob("*.apk"))
        if len(patched) != len(identities) or set(map(apk_identity, patched)) != set(identities):
            raise ValueError("Patching changed the package/version/split composition")
        base = next(path for path in patched if not apk_identity(path)[2])
        with zipfile.ZipFile(base) as archive:
            modules = [name for name in archive.namelist() if re.fullmatch(r"assets/lspatch/modules/[^/]+\.apk", name)]
            if len(modules) != 1:
                raise ValueError("The patched base must contain exactly one embedded module")
            with archive.open(modules[0]) as stream:
                if hashlib.file_digest(stream, "sha256").hexdigest() != digest(module):
                    raise ValueError("Embedded module differs from the verified release APK")
        for path in patched:
            aligned = subprocess.run(["zipalign", "-c", "-P", "16", "4", str(path)], capture_output=True)
            if aligned.returncode:
                replacement = path.with_suffix(".aligned.apk")
                run("zipalign", "-P", "16", "-f", "4", path, replacement)
                run("apksigner", "sign", "--ks", key, "--ks-pass", "env:TEMPLATE_KS_PASS", "--ks-key-alias",
                    env["TEMPLATE_KEY_ALIAS"], "--key-pass", "env:TEMPLATE_KEY_PASS", "--min-sdk-version", "28",
                    "--alignment-preserved", "true", "--v4-signing-enabled", "false", replacement, env=env)
                replacement.replace(path)
            run("zipalign", "-c", "-P", "16", "4", path)
            if signer(path) != expected:
                raise ValueError("Patched APK signing identity mismatch")
        bundle = stage / "candidate.apks"
        with zipfile.ZipFile(bundle, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path in patched:
                archive.write(path, path.name)
        with zipfile.ZipFile(bundle) as archive:
            if archive.testzip():
                raise ValueError("Bundle CRC validation failed")
        receipt = {"module_sha256": digest(module), "bundle_sha256": digest(bundle),
                   "certificate_sha256": expected, "local_test_signing": args.local_test_signing,
                   "identities": identities, "patcher": "LSPatch 1.2 build 487", "signature_level": 2,
                   "zip_alignment": 16384}
        bundle.replace(output)
    if args.receipt:
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"bundle": str(output), **receipt}, indent=2))


if __name__ == "__main__":
    main()
