package com.devin.lsposed.rogue;

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

    public static final String MENU_BUBBLE_TEXT = "Rwd";
    public static final String MENU_TITLE = "Rogue with the Dead Toolkit";
    public static final String MENU_SUBTITLE = "Time / ACTk / PAIRIP";

    /** Shows the floating button and movable rectangular menu inside the target Activity. */
    public static final boolean ENABLE_OVERLAY = true;


    /** Logs Activity.onResume events as a simple Java-hook smoke test. */
    public static final boolean ENABLE_SAMPLE_ACTIVITY_LOG_HOOK = true;

    /** Pulled from BuildConfig so release builds strip verbose logs automatically. */
    public static final boolean VERBOSE_LOGS = BuildConfig.VERBOSE_LOGS;

    /**
     * Log tag used everywhere in the module. Keep it bland; module-branded tags are trivially
     * greppable in logcat by target-app integrity sweeps.
     */
    public static final String LOG_TAG = "AppRuntime";

    /** Thread name used for deferred module initialization inside target processes. */
    public static final String WORKER_THREAD_NAME = "AppRuntimeWorker";

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
