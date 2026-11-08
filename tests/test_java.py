"""Host contract tests; Android builds separately validate the real API signatures."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import re

TESTS = Path(__file__).resolve().parent
ROOT = Path(os.environ.get("TEMPLATE_TEST_ROOT", TESTS.parent))


class JavaContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.classes = Path(cls.temp.name)
        namespace = re.search(r'namespace\s*=\s*"([^"]+)"', (ROOT / "app/build.gradle.kts").read_text())[1]
        sources = []
        for original in (TESTS / "java").rglob("*.java"):
            copy = cls.classes / "fixtures" / original.relative_to(TESTS / "java")
            copy.parent.mkdir(parents=True, exist_ok=True)
            copy.write_text(original.read_text().replace("com.template.lsposed", namespace))
            sources.append(copy)
        main = ROOT / "app/src/main/java" / namespace.replace(".", "/")
        overlay = (main / "ui/OverlayController.java").read_text()
        gesture_methods = overlay[overlay.index("    private void makeDraggable("):
                                  overlay.index("    private void hideOverlay(")]
        gesture = cls.classes / "GestureChecks.java"
        gesture.write_text('''import android.view.*;
import android.widget.FrameLayout;
public class GestureChecks {
 private int dp(float value){return (int)value;}
''' + gesture_methods + '''
 public static void main(String[] args){
  View view=new View();
  new GestureChecks().makeDraggable(view,view,new FrameLayout.LayoutParams(),true);
  view.listener.handle(view,new MotionEvent(MotionEvent.ACTION_DOWN,0));
  if(args[0].equals("drag")) view.listener.handle(view,new MotionEvent(MotionEvent.ACTION_MOVE,20));
  view.listener.handle(view,new MotionEvent(args[0].equals("cancel")?MotionEvent.ACTION_CANCEL:MotionEvent.ACTION_UP,20));
  int expected=args[0].equals("tap")?1:0;
  if(view.clicks!=expected) throw new AssertionError("Expected "+expected+" clicks, got "+view.clicks);
 }
}
''')
        sources.append(gesture)
        sources.extend(main / (name + ".java") for name in (
            "ModuleEntry", "FeatureRegistry", "FeatureState", "TemplateConfig"))
        sources.append(main / "engine/EngineDetector.java")
        result = subprocess.run(["javac", "--release", "17", "-d", str(cls.classes),
                                 *map(str, sources)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)

    def run_case(self, case):
        result = subprocess.run(["java", "-cp", str(self.classes), "ContractChecks", case],
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_gestures(self):
        for case in ("cancel", "tap", "drag"):
            with self.subTest(gesture=case):
                result = subprocess.run(["java", "-cp", str(self.classes), "GestureChecks", case],
                                        capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


for case in ("hook_retry", "hook_once", "nan_write", "infinite_write",
             "malformed_bool", "nan_load", "precision_roundtrip", "bounded_load", "finite_clamp",
             "integer_steps", "atomic_parse", "late_registration", "preview_commit", "late_context",
             "process_scope", "engine_java", "engine_native", "engine_javascript", "engine_react", "engine_unity_split"):
    setattr(JavaContractTests, "test_" + case, lambda self, case=case: self.run_case(case))


if __name__ == "__main__":
    unittest.main()
