#!/usr/bin/env python3
"""Load the shared Android identity, or run a build command with it. Never prints secrets."""
import json
import os
from pathlib import Path
import re
import sys

NAMES = ("TEMPLATE_KS_PATH", "TEMPLATE_KS_PASS", "TEMPLATE_KEY_ALIAS",
         "TEMPLATE_KEY_PASS", "TEMPLATE_CERT_SHA256")


def signing_environment(environment=None, *, required=True):
    env = dict(os.environ if environment is None else environment)
    if not any(env.get(name) for name in NAMES):
        path = Path(env.get("ANDROID_SIGNING_PROFILE", str(Path.home() / ".android/signing/jordan-modules.json")))
        if path.is_file():
            profile = json.loads(path.read_text())
            if profile.get("schema") != 1 or profile.get("storeType") != "PKCS12":
                raise ValueError("Unsupported Android signing profile")
            password = Path(profile["passwordFile"]).read_text().strip()
            env.update(TEMPLATE_KS_PATH=profile["keystore"], TEMPLATE_KS_PASS=password,
                       TEMPLATE_KEY_ALIAS=profile["alias"], TEMPLATE_KEY_PASS=password,
                       TEMPLATE_CERT_SHA256=profile["certificateSha256"])
    if required:
        if not all(env.get(name) for name in NAMES):
            raise ValueError("Shared signing requires all five TEMPLATE_* variables or a complete local profile")
        key = Path(env["TEMPLATE_KS_PATH"])
        if not key.is_absolute() or not key.is_file():
            raise ValueError("The shared keystore must be an existing absolute path")
        pin = env["TEMPLATE_CERT_SHA256"].replace(":", "").lower()
        if not re.fullmatch(r"[0-9a-f]{64}", pin):
            raise ValueError("Invalid shared certificate digest")
        env["TEMPLATE_CERT_SHA256"] = pin
    return env


def main():
    if len(sys.argv) < 2:
        raise SystemExit("Usage: shared_signing.py COMMAND [ARGUMENT ...]")
    try:
        env = signing_environment()
    except (OSError, ValueError, KeyError):
        raise SystemExit("Configure the shared Android signing profile or signing environment")
    os.execvpe(sys.argv[1], sys.argv[1:], env)


if __name__ == "__main__":
    main()
