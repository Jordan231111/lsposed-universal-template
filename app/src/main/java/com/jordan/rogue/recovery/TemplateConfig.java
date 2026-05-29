package com.jordan.rogue.recovery;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;

/**
 * Change these constants first when creating a new module.
 *
 * <p>The helper script in {@code tools/configure-template.py} rewrites the package name,
 * app name, module metadata, and static scope files for you.</p>
 */
public final class TemplateConfig {
    private TemplateConfig() {}

    /** Packages where the module is allowed to run. Keep this tight for stability. */
    public static final String[] TARGET_PACKAGES = {
            "net.room6.horizon"
    };

    /**
     * Optional allow-list of process suffixes to hook. Empty string = the main process
     * (bare package name). Example: {@code {"", ":game"}} hooks both {@code com.example.target}
     * and {@code com.example.target:game}. Use {@code {"*"}} to hook every process.
     */
    public static final String[] TARGET_PROCESS_SUFFIXES = {""};

    /**
     * Process suffixes that are never hooked, even if {@link #TARGET_PROCESS_SUFFIXES} matches.
     * These cover the most common anti-cheat / telemetry / pusher satellites that games ship.
     * Add your own; keep the defaults.
     */
    public static final String[] SKIP_PROCESS_SUFFIXES = {
            ":push", ":pushservice", ":pushcore",
            ":remote", ":service", ":core",
            ":msfcore", ":msf", ":mini",
            ":crashpad_handler", ":crash_handler", ":bugly", ":sentry",
            ":gameservice", ":anticheat", ":ac",
            ":sandboxed_process", ":isolated_process",
            ":privilege", ":stub"
    };

    public static final String TARGET_ACTIVITY_CLASS = "net.room6.horizon.MyActivity";

    public static final String MENU_BUBBLE_TEXT = "RWTD";
    public static final String MENU_TITLE = "Rogue Recovery";
    public static final String MENU_SUBTITLE = "Recovered LSPosed scaffold";

    /** Shows the floating button and movable rectangular menu inside the target Activity. */
    public static final boolean ENABLE_OVERLAY = true;

    /** Loads app/src/main/cpp and initializes ByteDance ShadowHook. Requires arm/arm64 process. */
    public static final boolean ENABLE_NATIVE_SHADOWHOOK = true;

    /** Recovered from the old module: hook MyActivity.onCreate(Bundle) and start from Activity context. */
    public static final boolean ENABLE_ROGUE_ACTIVITY_HOOK = true;

    /**
     * Bypass PAIRIP (Play Application Integrity Protection) signature/license checks plus the
     * Java-layer Google Play Integrity surface. Required if you want the app to start cleanly on
     * a rooted/emulated device where the licensing service bind fails. Cloud-save has separate
     * native IL2CPP hooks documented in docs/INTEGRITY_BYPASS_NOTES.md.
     */
    public static final boolean ENABLE_INTEGRITY_BYPASS = true;

    /**
     * Native cloud-save integrity repair. This does not fake a final success state; it injects
     * the app's Google cloud project number into the IL2CPP IntegrityTokenRequest so the game's
     * own RequiredIntegrityCheck path can request a server-consistent token.
     */
    public static final boolean ENABLE_SERVER_INTEGRITY_BYPASS = true;

    /**
     * Neutralise CodeStage Anti-Cheat Toolkit detectors (Injection / ObscuredCheating / SpeedHack
     * / TimeCheating / WallHack). Most builds expose detectors only via IL2CPP so this flag also
     * gates the native silencing in template_native.cpp.
     */
    public static final boolean ENABLE_ACTK_BYPASS = true;

    /**
     * Forge a successful IssueBackupKey/VerifyBackupKey response when the real PlayFab CloudScript
     * call comes back with a non-success code (most commonly
     * {@code VerifyIntegrityVerdictUnevaluated} on emulators Google cannot evaluate). The native
     * layer rewrites the awaitable closure's {@code errorCode} to RogueServerCode.Success and the
     * {@code msg} field to a freshly generated backup code, so the UI shows a code instead of the
     * register-failed error toast. The forged code does not round-trip through PlayFab; the
     * matching forge on the restore device makes VerifyBackupKey accept any input. Documented in
     * docs/INTEGRITY_BYPASS_NOTES.md.
     */
    public static final boolean ENABLE_FORGE_BACKUP_SUCCESS = true;

    /**
     * Default game speed multiplier. {@code 1.0} = no override (game runs at its normal pace).
     * The default is intentionally neutral; persisted user state from {@code .rt_state} supplies
     * the launch value. Anything higher overrides {@code UseCase_SwitchGameSpeed.speedUpValue}
     * after the SDK has computed its own value, and safe IL2CPP callbacks re-apply the override
     * automatically after launch and world switches. The slider in the floating panel writes the
     * live value through to the IL2CPP field on every onProgressChanged event.
     */
    public static final float DEFAULT_GAME_SPEED_MULTIPLIER = 1f;

    /** Pulled from BuildConfig so release builds strip verbose logs automatically. */
    public static final boolean VERBOSE_LOGS = BuildConfig.VERBOSE_LOGS;

    /** Enable verbose ShadowHook logs only while debugging. */
    public static final boolean SHADOWHOOK_DEBUG_LOGS = VERBOSE_LOGS;

    /** Operation records are useful while developing native hooks. */
    public static final boolean SHADOWHOOK_RECORDS = VERBOSE_LOGS;

    /**
     * Log tag used everywhere in the module. Keep it bland; module-branded tags are trivially
     * greppable in logcat by target-app integrity sweeps.
     */
    public static final String LOG_TAG = "AppRuntime";

    /** Thread name used for deferred module initialization inside target processes. */
    public static final String WORKER_THREAD_NAME = "AppRuntimeWorker";

    /**
     * Name of the native library loaded by {@link NativeBridge}. The tooling script can rename
     * this plus the CMake target and {@code System.loadLibrary} call with {@code --native-lib}.
     */
    public static final String NATIVE_LIBRARY_NAME = "rogue_recovery";

    /** Best-effort runtime toggle state filename under the target app's files directory. */
    public static final String FEATURE_STATE_FILE_NAME = ".rt_state";

    public static boolean shouldHook(String packageName) {
        if (packageName == null) return false;
        for (String target : TARGET_PACKAGES) {
            if (packageName.equals(target)) return true;
        }
        return false;
    }

    /**
     * Process-level filter applied after {@link #shouldHook(String)}. The {@code processName}
     * argument is the value returned by {@code ModuleLoadedParam.getProcessName()}
     * (e.g. {@code "com.example.target"}, {@code "com.example.target:push"}).
     */
    public static boolean shouldHookProcess(String packageName, String processName) {
        if (!shouldHook(packageName)) return false;
        String suffix = processSuffix(packageName, processName);

        List<String> skip = Arrays.asList(SKIP_PROCESS_SUFFIXES);
        if (skip.contains(suffix)) return false;

        List<String> allow = TARGET_PROCESS_SUFFIXES == null
                ? Collections.emptyList()
                : Arrays.asList(TARGET_PROCESS_SUFFIXES);
        if (allow.isEmpty()) return suffix.isEmpty();
        if (allow.contains("*")) return true;
        return allow.contains(suffix);
    }

    private static String processSuffix(String packageName, String processName) {
        if (processName == null || processName.isEmpty()) return "";
        if (packageName == null) return processName;
        if (!processName.startsWith(packageName)) return processName;
        return processName.substring(packageName.length());
    }
}
