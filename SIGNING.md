# Android signing

All local module APKs, patched game splits and the MicroG-RE companion use this identity:

| Setting | Value |
| --- | --- |
| Keystore | `/Users/jordan/.android/signing/jordan-modules.p12` |
| Local build profile | `/Users/jordan/.android/signing/jordan-modules.json` |
| Alias | `jordan-modules` |
| Format / key | PKCS12 / RSA 4096 |
| Certificate signature | SHA256withRSA |
| Valid until | 2036-11-25 17:56:13 UTC |
| Certificate SHA-256 | `5486b776291d98b9bc5a70586b9af89afce8674fd462889238c0ebd99786a1c1` |

Gradle and the packaging tools read the local profile automatically. It points to an owner-only
password file in the same directory. Keep all three files outside Git. Select another profile
location with `ANDROID_SIGNING_PROFILE` when building on a different machine.

CI stages the same key from the repository secrets `TEMPLATE_KS_BASE64`, `TEMPLATE_KS_PASS`,
`TEMPLATE_KEY_ALIAS` and `TEMPLATE_KEY_PASS`. The public repository variable
`TEMPLATE_CERT_SHA256` contains the certificate above. `tools/setup_shared_signing.py` verifies
the staged key and removes it on failure; workflow cleanup removes it after the build.

Command-line packaging also accepts the complete `TEMPLATE_KS_PATH`, `TEMPLATE_KS_PASS`,
`TEMPLATE_KEY_ALIAS`, `TEMPLATE_KEY_PASS` and `TEMPLATE_CERT_SHA256` environment. Partial
configuration fails. Passwords pass through environment variables and stay out of process arguments.

Use the module's documented Gradle task for the release APK. Original publisher certificates are
derived or verified separately when preparing game hosts. Install every patched split together.
APK ZIP alignment and native ELF segment alignment require independent verification.
