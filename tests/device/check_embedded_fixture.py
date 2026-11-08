#!/usr/bin/env python3
"""Exercise only the uniquely named owned fixture produced by build_embedded_fixture.py."""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import time
import uuid
import xml.etree.ElementTree as ET


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--fixture", required=True, type=Path)
    p.add_argument("--device", required=True)
    args = p.parse_args()
    fixture = json.loads(args.fixture.read_text())
    host, module = fixture["host_package"], fixture["module_package"]
    assert re.fullmatch(r"org\.template\.audit\.run[0-9a-f]{12}\.host", host), "Only owned generated fixtures"
    assert module == host.removesuffix(".host") + ".module"
    remote = "/data/local/tmp/template-ui-" + uuid.uuid4().hex + ".xml"
    checks = []

    def adb(*parts):
        result = subprocess.run(["adb", "-s", args.device, *map(str, parts)], check=True,
                                capture_output=True, text=True, timeout=30)
        return result.stdout

    def tree():
        adb("shell", "uiautomator", "dump", remote)
        return ET.fromstring(adb("shell", "cat", remote))

    def find(root, text):
        matches = [n for n in root.iter("node") if n.get("text") == text]
        assert len(matches) == 1, (text, [n.get("text") for n in root.iter("node") if n.get("text")])
        return matches[0]

    def tap(node):
        x1, y1, x2, y2 = map(int, re.findall(r"\d+", node.get("bounds")))
        adb("shell", "input", "tap", (x1 + x2) // 2, (y1 + y2) // 2)

    def click(text):
        tap(find(tree(), text))

    def open_panel():
        root = tree()
        if not any(n.get("text") == "Template QA" for n in root.iter("node")):
            tap(find(root, "Nyx"))

    def toggle(label):
        root = tree()
        for parent in root.iter("node"):
            if any(n.get("text") == label for n in parent):
                tap(next(n for n in parent if n.get("class") == "android.widget.Button"))
                return
        raise AssertionError(label)

    def values():
        command = f"cat /data/user/0/{host}/files/.{module}.settings.json"
        return json.loads(adb("shell", "su", "-c", shlex.quote(command)))

    def enter(value):
        edit = next(n for n in tree().iter("node") if n.get("class") == "android.widget.EditText")
        tap(edit)
        adb("shell", "input", "keyevent", "123")
        adb("shell", "input", "keyevent", *(["67"] * 24))
        adb("shell", "input", "text", value)
        find(tree(), value)
        click("APPLY")

    def start():
        adb("shell", "am", "start", "-W", "-n", host + "/" + fixture["activity"])

    try:
        adb("shell", "am", "force-stop", host)
        start(); open_panel(); click("Reset"); click("Status")
        assert any("Native helpers: available" in n.get("text", "") for n in tree().iter("node"))
        checks.append("API102 embedded bootstrap and native JNI status")
        click("Example multiplier: 1.0")
        enter("5.4")
        # Android's soft keyboard can remain visible after dismissing the dialog.
        adb("shell", "input", "keyevent", "111")
        find(tree(), "Example multiplier: 5.4")
        click("QA integer: 3.0")
        enter("7.6")
        adb("shell", "input", "keyevent", "111")
        find(tree(), "QA integer: 8.0")
        toggle("Module enabled")
        state = values()
        assert state["enabled"] is False and abs(state["multiplier"] - 5.4) < 1e-5 and state["qa_integer"] == 8
        checks.append("independent numeric controls, exact input, integer normalization and bool persistence")
        click("X")
        adb("shell", "am", "force-stop", host); start(); open_panel()
        find(tree(), "Example multiplier: 5.4"); find(tree(), "QA integer: 8.0")
        assert values() == state
        toggle("Module enabled"); click("X")
        checks.append("cold restart restores all feature values")
        for action in ("RECREATE ACTIVITY", "PORTRAIT", "LANDSCAPE"):
            click(action); open_panel()
            find(tree(), "Example multiplier: 5.4"); find(tree(), "QA integer: 8.0")
            click("X")
        checks.append("Activity recreation and both orientations")
        adb("shell", "input", "keyevent", "3"); start(); open_panel()
        find(tree(), "Example multiplier: 5.4"); find(tree(), "QA integer: 8.0")
        click("Reset"); find(tree(), "Example multiplier: 1.0"); find(tree(), "QA integer: 3.0")
        assert values()["enabled"] is True
        checks.append("background/resume and reset refresh every control")
        click("X")
        receipt = {"passed": True, "checks": checks, "device_abis": adb("shell", "getprop", "ro.product.cpu.abilist").strip()}
        args.fixture.with_name("device.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(json.dumps(receipt, indent=2))
    finally:
        adb("shell", "rm", "-f", remote)


if __name__ == "__main__":
    main()
