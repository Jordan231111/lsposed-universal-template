#!/usr/bin/env python3
"""Stage the selected CI signing identity without putting credentials in the checkout."""
import base64
import hashlib
import os
from pathlib import Path
import re
import subprocess


def main():
    names = ("TEMPLATE_KS_BASE64", "TEMPLATE_KS_PASS", "TEMPLATE_KEY_ALIAS",
             "TEMPLATE_KEY_PASS", "TEMPLATE_CERT_SHA256")
    values = {name: os.environ.get(name, "") for name in names}
    if any(not value or "\n" in value or "\r" in value for value in values.values()):
        raise SystemExit("All shared signing secrets and the public certificate pin are required")
    expected = values["TEMPLATE_CERT_SHA256"].replace(":", "").lower()
    if not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise SystemExit("Invalid shared certificate digest")
    destination = Path(os.environ["RUNNER_TEMP"]) / "shared-android-signing.p12"
    descriptor = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(base64.b64decode(values["TEMPLATE_KS_BASE64"], validate=True))
        result = subprocess.run(["keytool", "-exportcert", "-keystore", str(destination),
            "-storepass:env", "TEMPLATE_KS_PASS", "-alias", values["TEMPLATE_KEY_ALIAS"]],
            capture_output=True, check=True)
        if hashlib.sha256(result.stdout).hexdigest() != expected:
            raise ValueError("Shared signing certificate mismatch")
        with Path(os.environ["GITHUB_ENV"]).open("a") as environment:
            for name in names[1:]:
                environment.write(name + "=" + values[name] + "\n")
            environment.write("TEMPLATE_KS_PATH=" + str(destination) + "\n")
    except Exception:
        destination.unlink(missing_ok=True)
        raise SystemExit("Unable to stage and verify the shared signing identity")
    print("Verified shared Android signing identity")


if __name__ == "__main__":
    main()
