#!/usr/bin/env python3
"""Build an owned two-APK host and a uniquely configured module; package without touching apps."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="New directory for this isolated QA build")
    args = parser.parse_args()
    work = args.output.resolve()
    work.mkdir(parents=True, exist_ok=False)
    run_id = uuid.uuid4().hex[:12]
    package = "org.template.audit.run" + run_id
    host, module = package + ".host", package + ".module"
    sdk = Path(os.environ.get("ANDROID_HOME", Path.home() / "Library/Android/sdk"))
    platform = sdk / "platforms/android-37.0/android.jar"
    tools = sdk / "build-tools/37.0.0"
    log = work / "build.log"

    def run(*parts, cwd=None):
        with log.open("a") as stream:
            subprocess.run(list(map(str, parts)), cwd=cwd, stdout=stream, stderr=subprocess.STDOUT, check=True)

    fixture = work / "host"
    fixture.mkdir()
    source = fixture / "MainActivity.java"
    source.write_text('''package org.template.audit.host;
import android.app.Activity;
import android.os.Bundle;
import android.widget.*;
import android.content.pm.ActivityInfo;
public final class MainActivity extends Activity {
  @Override public void onCreate(Bundle state) {
    super.onCreate(state);
    LinearLayout layout = new LinearLayout(this); layout.setOrientation(1);
    layout.setPadding(24, 120, 24, 24);
    TextView title = new TextView(this); title.setText("Owned template QA host"); layout.addView(title);
    Button recreate = new Button(this); recreate.setText("Recreate Activity");
    recreate.setOnClickListener(v -> recreate()); layout.addView(recreate);
    Button landscape = new Button(this); landscape.setText("Landscape");
    landscape.setOnClickListener(v -> setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)); layout.addView(landscape);
    Button portrait = new Button(this); portrait.setText("Portrait");
    portrait.setOnClickListener(v -> setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)); layout.addView(portrait);
    setContentView(layout);
  }
}
''')
    classes, dex = fixture / "classes", fixture / "dex"
    classes.mkdir(); dex.mkdir()
    run("javac", "-source", "17", "-target", "17", "-classpath", platform, "-d", classes, source)
    run(tools / "d8", "--min-api", "28", "--lib", platform, "--output", dex, *classes.rglob("*.class"))
    apks = []
    for split in ("", "config.qa"):
        name = "split_config.qa" if split else "base"
        manifest = fixture / (name + ".xml")
        manifest.write_text(f'''<manifest xmlns:android="http://schemas.android.com/apk/res/android"
 package="{host}" android:versionCode="1" android:versionName="1.0" {('split="' + split + '"') if split else ''}>
 <uses-sdk android:minSdkVersion="28" android:targetSdkVersion="37"/>
 <application android:label="Template QA Host" android:debuggable="true" android:hasCode="{'false' if split else 'true'}">
 {'' if split else '<activity android:name="org.template.audit.host.MainActivity" android:exported="true"><intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.LAUNCHER"/></intent-filter></activity>'}
 </application></manifest>''')
        unsigned, aligned, signed = [fixture / (name + suffix) for suffix in (".unsigned.apk", ".aligned.apk", ".apk")]
        run(tools / "aapt2", "link", "--manifest", manifest, "-I", platform, "-o", unsigned)
        if not split:
            with zipfile.ZipFile(unsigned, "a") as archive:
                archive.write(dex / "classes.dex", "classes.dex")
        run(tools / "zipalign", "-P", "16", "4", unsigned, aligned)
        run(tools / "apksigner", "sign", "--ks", Path.home() / ".android/debug.keystore", "--ks-pass", "pass:android",
            "--key-pass", "pass:android", "--out", signed, aligned)
        apks.append(signed)
    copy = work / "module"
    shutil.copytree(ROOT, copy, ignore=shutil.ignore_patterns(".git", ".gradle", "build", ".cxx", "artifacts", "__pycache__"))
    run(sys.executable, copy / "tools/configure-template.py", "--package", module, "--name", "Template QA",
        "--target", host, "--author", "Fixture", "--native-lib", "qa_runtime")
    registry = copy / "app/src/main/java" / module.replace(".", "/") / "FeatureRegistry.java"
    registry.write_text(registry.read_text().replace('            initialized = true;',
        '            register(Feature.integer("qa_integer", "QA integer", 3, 1, 100));\n            initialized = true;'))
    run(copy / "gradlew", ":app:assembleRelease", "-Ptemplate.localTestSigning=true", cwd=copy)
    release = copy / "app/build/outputs/apk/release/app-release.apk"
    run(sys.executable, ROOT / "tools/package-lspatch.py", "--module", release, "--local-test-signing",
        "--output", work / "fixture.apks", "--receipt", work / "bundle.json", *apks)
    receipt = {"host_package": host, "module_package": module, "activity": "org.template.audit.host.MainActivity",
               "bundle": str(work / "fixture.apks"), "module": str(release), "originals": list(map(str, apks))}
    (work / "fixture.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
