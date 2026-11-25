"""A cached Gradle configuration must not authorize replaced signing material."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SigningCacheTests(unittest.TestCase):
    def test_replaced_key_is_rechecked_with_configuration_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            key, password, profile = [root / name for name in ("identity.p12", "password", "profile.json")]
            password.write_text("fixture-only\n")
            (root / "settings.gradle").write_text("rootProject.name = 'signing-cache-fixture'\n")
            helper = (ROOT / "gradle/shared-signing.gradle").as_posix().replace("'", "\\'")
            (root / "build.gradle").write_text("apply from: '" + helper + "'\n")

            def replace_key(label):
                key.unlink(missing_ok=True)
                subprocess.run(["keytool", "-genkeypair", "-noprompt", "-keystore", str(key),
                    "-storetype", "PKCS12", "-storepass", "fixture-only", "-keypass", "fixture-only",
                    "-alias", "fixture", "-keyalg", "RSA", "-keysize", "2048", "-validity", "1",
                    "-dname", "CN=" + label], check=True, capture_output=True)
                cert = subprocess.check_output(["keytool", "-exportcert", "-keystore", str(key),
                    "-storepass", "fixture-only", "-alias", "fixture"], stderr=subprocess.DEVNULL)
                return hashlib.sha256(cert).hexdigest()

            pin = replace_key("First fixture")
            profile.write_text(json.dumps({"schema": 1, "storeType": "PKCS12", "keystore": str(key),
                "passwordFile": str(password), "alias": "fixture", "certificateSha256": pin}))
            env = {name: value for name, value in os.environ.items() if not name.startswith("TEMPLATE_")}
            env["ANDROID_SIGNING_PROFILE"] = str(profile)
            command = ["bash", str(ROOT / "gradlew"), "--configuration-cache", "--console=plain",
                       "verifySharedAndroidSigning"]
            first = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True, timeout=180)
            self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
            self.assertNotEqual(replace_key("Replacement fixture"), pin)
            second = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True, timeout=180)
            self.assertNotEqual(second.returncode, 0)
            self.assertIn("Reusing configuration cache", second.stdout)
            self.assertIn("differs from its configured identity", second.stdout + second.stderr)
