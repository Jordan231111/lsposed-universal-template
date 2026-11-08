import com.template.lsposed.FeatureRegistry;
import com.template.lsposed.ModuleEntry;
import com.template.lsposed.TemplateConfig;
import com.template.lsposed.engine.EngineDetector;
import io.github.libxposed.api.XposedModule;
import java.io.File;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.util.Map;

/** Each case runs in a fresh JVM so process-scoped state matches a fresh app process. */
public class ContractChecks {
    private static void check(boolean condition, String description) {
        if (!condition) throw new AssertionError(description);
    }

    private static void parse(String text) throws Exception {
        Method method = FeatureRegistry.class.getDeclaredMethod("parseAndApply", String.class);
        method.setAccessible(true);
        method.invoke(null, text);
    }

    private static File storage() throws Exception {
        File file = Files.createTempDirectory("feature-state").resolve("state.json").toFile();
        Field field = FeatureRegistry.class.getDeclaredField("storage");
        field.setAccessible(true);
        field.set(null, file);
        file.getParentFile().deleteOnExit();
        file.deleteOnExit();
        return file;
    }

    private static void load() throws Exception {
        Method method = FeatureRegistry.class.getDeclaredMethod("load");
        method.setAccessible(true);
        method.invoke(null);
    }

    @SuppressWarnings("unchecked")
    public static void main(String[] args) throws Exception {
        String test = args[0];
        if (test.startsWith("hook")) {
            XposedModule.failFirst = test.equals("hook_retry");
            ModuleEntry module = new ModuleEntry();
            module.onModuleLoaded(() -> TemplateConfig.TARGET_PACKAGES[0]);
            module.onPackageLoaded(() -> TemplateConfig.TARGET_PACKAGES[0]);
            module.onPackageReady(() -> TemplateConfig.TARGET_PACKAGES[0]);
            System.out.println("attempts=" + XposedModule.attempts + " installed=" + XposedModule.installed);
            for (String method : new String[]{"attach"}) {
                check(XposedModule.installed.getOrDefault(method, 0) == 1,
                      "Each hook must register once; a failed registration must retry at package-ready");
            }
            check(!XposedModule.installed.containsKey("onResume"),
                  "The module must not install the Activity smoke-test hook");
            return;
        }
        if (test.equals("process_scope")) {
            String target = TemplateConfig.TARGET_PACKAGES[0];
            check(!TemplateConfig.shouldHookProcess(target, null), "Unknown process is not assumed to be main");
            String original = TemplateConfig.TARGET_PROCESS_SUFFIXES[0];
            TemplateConfig.TARGET_PROCESS_SUFFIXES[0] = "*";
            check(!TemplateConfig.shouldHookProcess(target, target + ":sandboxed_process0:renderer"), "Indexed sandbox process remains excluded");
            check(!TemplateConfig.shouldHookProcess(target, target + ":isolated_process4"), "Indexed isolated process remains excluded");
            check(TemplateConfig.shouldHookProcess(target, target), "Main process remains eligible");
            TemplateConfig.TARGET_PROCESS_SUFFIXES[0] = original;
            return;
        }
        if (test.startsWith("engine_")) {
            File directory = Files.createTempDirectory("engine-fixture").toFile();
            File apk = new File(directory, "base.apk");
            File split = new File(directory, "split.apk");
            try (var zip = new java.util.zip.ZipOutputStream(new java.io.FileOutputStream(apk))) {
                zip.putNextEntry(new java.util.zip.ZipEntry("assets/placeholder")); zip.closeEntry();
            }
            String library = switch (test) {
                case "engine_unity_split" -> "libil2cpp.so";
                case "engine_javascript" -> "libjsc.so";
                case "engine_react" -> "libreactnative.so";
                case "engine_native" -> "libcustom.so";
                default -> null;
            };
            if (library != null) try (var zip = new java.util.zip.ZipOutputStream(new java.io.FileOutputStream(split))) {
                zip.putNextEntry(new java.util.zip.ZipEntry("lib/arm64-v8a/" + library)); zip.closeEntry();
            }
            var info = new android.content.pm.ApplicationInfo();
            info.sourceDir = apk.getPath();
            info.splitSourceDirs = library == null ? null : new String[]{split.getPath()};
            var context = new android.content.Context() {
                @Override public android.content.pm.ApplicationInfo getApplicationInfo() { return info; }
            };
            EngineDetector.Engine expected = switch (test) {
                case "engine_unity_split" -> EngineDetector.Engine.UNITY;
                case "engine_javascript" -> EngineDetector.Engine.JAVASCRIPT;
                case "engine_react" -> EngineDetector.Engine.REACT_NATIVE;
                case "engine_native" -> EngineDetector.Engine.NATIVE;
                default -> EngineDetector.Engine.JAVA;
            };
            check(EngineDetector.detect(context) == expected, "Packaged engine evidence: " + test);
            if (split.exists()) check(split.delete(), "Remove split fixture");
            check(apk.delete() && directory.delete(), "Remove engine fixture");
            return;
        }
        FeatureRegistry.initialize(null);
        switch (test) {
            case "nan_write":
                FeatureRegistry.setFloat("multiplier", 2f);
                FeatureRegistry.setFloat("multiplier", Float.NaN);
                check(FeatureRegistry.getFloat("multiplier") == 2f, "NaN must not replace a finite setting");
                break;
            case "infinite_write":
                FeatureRegistry.setFloat("multiplier", 2f);
                FeatureRegistry.setFloat("multiplier", Float.POSITIVE_INFINITY);
                check(FeatureRegistry.getFloat("multiplier") == 2f, "Infinity must not replace a finite setting");
                break;
            case "malformed_bool":
                parse("{\"enabled\":garbage}");
                check(FeatureRegistry.getBool("enabled"), "Malformed booleans must not disable the module");
                break;
            case "nan_load":
                parse("{\"multiplier\":NaN}");
                check(FeatureRegistry.getFloat("multiplier") == 1f, "Non-finite persisted values must be ignored");
                break;
            case "precision_roundtrip":
                storage();
                String preciseKey = "precise_value";
                FeatureRegistry.register(FeatureRegistry.Feature.number(preciseKey, "Precise", 1f, 1f, 30f));
                float original = 1.2345678f;
                FeatureRegistry.setFloat(preciseKey, original);
                Field field = FeatureRegistry.class.getDeclaredField("FLOATS");
                field.setAccessible(true);
                ((Map<String, Float>) field.get(null)).put(preciseKey, 1f);
                load();
                float restored = FeatureRegistry.getFloat(preciseKey);
                System.out.println("saved=" + original + " restored=" + restored);
                check(restored == original, "Persistence must preserve the float value exactly");
                break;
            case "bounded_load":
                File file = storage();
                Files.writeString(file.toPath(), "{\"multiplier\":3,\"padding\":" + " ".repeat(70_000) + "0}");
                load();
                check(FeatureRegistry.getFloat("multiplier") == 1f, "Ignore oversized state files before applying values");
                break;
            case "finite_clamp":
                FeatureRegistry.setFloat("multiplier", -2f);
                check(FeatureRegistry.getFloat("multiplier") == 1f, "Lower bound");
                FeatureRegistry.setFloat("multiplier", 300f);
                check(FeatureRegistry.getFloat("multiplier") == 30f, "Upper bound");
                parse("{\"enabled\":false,\"multiplier\":2.5}");
                check(!FeatureRegistry.getBool("enabled") && FeatureRegistry.getFloat("multiplier") == 2.5f,
                      "Valid existing state stays compatible");
                break;
            case "integer_steps":
                FeatureRegistry.register(FeatureRegistry.Feature.integer("integer", "Integer", 1, 1, 100));
                FeatureRegistry.setFloat("integer", 17.6f);
                check(FeatureRegistry.getFloat("integer") == 18f, "Whole-number step");
                parse("{\"integer\":19.2}");
                check(FeatureRegistry.getFloat("integer") == 19f, "Stored and live values share normalization");
                FeatureRegistry.setFloat("integer", 200f);
                check(FeatureRegistry.getFloat("integer") == 100f, "Inclusive maximum");
                boolean rejectedWideInteger = false;
                try { FeatureRegistry.Feature.integer("wide", "Wide", 1, 1, 20_000_000); }
                catch (IllegalArgumentException expected) { rejectedWideInteger = true; }
                check(rejectedWideInteger, "Float-backed integer controls must reject inexact integer ranges");
                break;
            case "atomic_parse":
                parse("{\"enabled\":false,\"multiplier\":}");
                check(FeatureRegistry.getBool("enabled"), "Invalid document must not partially apply");
                parse("{\"enabled\":false,\"enabled\":true}");
                check(FeatureRegistry.getBool("enabled"), "Duplicate keys are rejected atomically");
                parse("{\"enabled\":false,}");
                check(FeatureRegistry.getBool("enabled"), "Trailing comma is rejected");
                break;
            case "late_registration":
                parse("{\"later\":2.5}");
                FeatureRegistry.register(FeatureRegistry.Feature.number("later", "Later", 1f, 1f, 10f));
                check(FeatureRegistry.getFloat("later") == 2.5f, "Late registration must retain loaded state");
                FeatureRegistry.setFloat("later", 3f);
                FeatureRegistry.register(FeatureRegistry.Feature.number("later", "Renamed", 1f, 1f, 10f));
                check(FeatureRegistry.getFloat("later") == 3f, "Re-registering must not overwrite current state");
                break;
            case "preview_commit":
                File destination = storage();
                FeatureRegistry.previewFloat("multiplier", 2f);
                FeatureRegistry.previewFloat("multiplier", 3f);
                check(!destination.exists(), "Slider previews must avoid synchronous file writes");
                FeatureRegistry.commit();
                String prior = Files.readString(destination.toPath());
                File temporary = new File(destination.getPath() + ".tmp");
                check(temporary.mkdir(), "Create write failure fixture");
                FeatureRegistry.setFloat("multiplier", 4f);
                check(Files.readString(destination.toPath()).equals(prior), "Failed save must preserve old file");
                check(temporary.delete(), "Remove failure fixture");
                FeatureRegistry.commit();
                check(Files.readString(destination.toPath()).contains("4.0"), "Failed save remains retryable without another value change");
                break;
            case "late_context":
                File directory = Files.createTempDirectory("late-context").toFile();
                File settings = new File(directory, com.template.lsposed.TemplateConfig.FEATURE_STATE_FILE_NAME);
                Files.writeString(settings.toPath(), "{\"multiplier\":3.5}");
                FeatureRegistry.initialize(new android.content.Context() {
                    @Override public File getFilesDir() { return directory; }
                });
                check(FeatureRegistry.getFloat("multiplier") == 3.5f, "Context-free initialization must not suppress later load");
                check(settings.delete() && directory.delete(), "Clean context fixture");
                break;
            default: throw new IllegalArgumentException(test);
        }
        System.out.println(test + ": passed");
    }
}
