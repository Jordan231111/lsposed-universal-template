"""The selected profile is atomic, and partial environment input cannot change identities."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("shared_identity", ROOT / "tools/shared_signing.py")
SIGNING = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SIGNING)


class SharedSigningTests(unittest.TestCase):
    def test_profile_and_partial_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            key, password, profile = [root / name for name in ("identity.p12", "password", "profile.json")]
            key.write_bytes(b"key-material-is-validated-by-the-signing-tools")
            password.write_text("fixture-value\n")
            profile.write_text(json.dumps({"schema": 1, "storeType": "PKCS12", "keystore": str(key),
                "passwordFile": str(password), "alias": "selected", "certificateSha256": "a" * 64}))
            environment = {"ANDROID_SIGNING_PROFILE": str(profile)}
            loaded = SIGNING.signing_environment(environment)
            self.assertEqual(loaded["TEMPLATE_KS_PATH"], str(key))
            self.assertEqual(loaded["TEMPLATE_KS_PASS"], "fixture-value")
            self.assertEqual(loaded["TEMPLATE_KEY_ALIAS"], "selected")
            self.assertNotIn("TEMPLATE_KS_PASS", environment)
            for name in SIGNING.NAMES:
                with self.subTest(name=name), self.assertRaises(ValueError):
                    SIGNING.signing_environment({**environment, name: "partial"})
            profile.write_text(json.dumps({"schema": 2, "storeType": "PKCS12"}))
            with self.assertRaisesRegex(ValueError, "Unsupported"):
                SIGNING.signing_environment(environment)

    def test_absent_profile_and_invalid_pin_fail(self):
        with self.assertRaises(ValueError):
            SIGNING.signing_environment({"ANDROID_SIGNING_PROFILE": "/absent/shared-signing.json"})
        with tempfile.TemporaryDirectory() as directory:
            key = Path(directory) / "key.p12"
            key.touch()
            environment = dict(zip(SIGNING.NAMES, (str(key), "p", "alias", "p", "invalid")))
            with self.assertRaisesRegex(ValueError, "digest"):
                SIGNING.signing_environment(environment)
