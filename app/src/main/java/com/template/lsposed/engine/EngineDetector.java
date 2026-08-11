package com.template.lsposed.engine;

import android.content.Context;
import android.content.pm.ApplicationInfo;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * Best-effort identification of the target app's runtime engine. Used so hooks can pick the
 * right strategy quickly (e.g. Il2Cpp helpers for Unity, BP_UObject for Unreal, dart vm for
 * Flutter). Falls back to {@link Engine#NATIVE} when no well-known library is loaded.
 *
 * <p>Checks the extracted {@code nativeLibraryDir}, native entries in the base/split APKs, and
 * finally {@code /proc/self/maps}. Split APK inspection matters on modern Android, where native
 * libraries commonly execute directly from an APK and their individual names are not visible in
 * the maps path.</p>
 */
public final class EngineDetector {

    public enum Engine {
        UNITY, UNREAL, COCOS2DX, GODOT, FLUTTER, REACT_NATIVE, XAMARIN, NATIVE, UNKNOWN
    }

    private static final Map<String, Engine> LIB_TO_ENGINE = new LinkedHashMap<>();

    static {
        // Order matters for ambiguous libs: check more specific names first.
        LIB_TO_ENGINE.put("libil2cpp.so", Engine.UNITY);
        LIB_TO_ENGINE.put("libunity.so", Engine.UNITY);
        LIB_TO_ENGINE.put("libUE4.so", Engine.UNREAL);
        LIB_TO_ENGINE.put("libUnreal.so", Engine.UNREAL);
        LIB_TO_ENGINE.put("libcocos2djs.so", Engine.COCOS2DX);
        LIB_TO_ENGINE.put("libcocos2dcpp.so", Engine.COCOS2DX);
        LIB_TO_ENGINE.put("libgodot_android.so", Engine.GODOT);
        LIB_TO_ENGINE.put("libflutter.so", Engine.FLUTTER);
        LIB_TO_ENGINE.put("libreactnativejni.so", Engine.REACT_NATIVE);
        LIB_TO_ENGINE.put("libjsc.so", Engine.REACT_NATIVE);
        LIB_TO_ENGINE.put("libhermes.so", Engine.REACT_NATIVE);
        LIB_TO_ENGINE.put("libmonodroid.so", Engine.XAMARIN);
        LIB_TO_ENGINE.put("libmonosgen-2.0.so", Engine.XAMARIN);
    }

    private EngineDetector() {}

    public static Engine detect(Context context) {
        for (String lib : evidence(context)) {
            Engine engine = LIB_TO_ENGINE.get(lib);
            if (engine != null) return engine;
        }
        return Engine.NATIVE;
    }

    /** Returns matching packaged and currently mapped libraries in deterministic priority order. */
    public static List<String> evidence(Context context) {
        Set<String> matches = new LinkedHashSet<>(evidenceFromNativeLibraryDir(context));
        matches.addAll(evidenceFromApkSplits(context));
        matches.addAll(evidenceFromProcessMaps());
        List<String> ordered = new ArrayList<>();
        for (String lib : LIB_TO_ENGINE.keySet()) {
            if (matches.contains(lib)) ordered.add(lib);
        }
        return ordered;
    }

    /** Returns a human-readable library list that matched, useful for overlay display. */
    public static List<String> evidenceFromNativeLibraryDir(Context context) {
        if (context == null) return java.util.Collections.emptyList();
        ApplicationInfo info = context.getApplicationInfo();
        if (info == null || info.nativeLibraryDir == null) return java.util.Collections.emptyList();
        File dir = new File(info.nativeLibraryDir);
        String[] files = dir.isDirectory() ? dir.list() : null;
        if (files == null || files.length == 0) return java.util.Collections.emptyList();
        Set<String> fileSet = new HashSet<>(Arrays.asList(files));
        List<String> present = new java.util.ArrayList<>();
        for (String lib : LIB_TO_ENGINE.keySet()) {
            if (fileSet.contains(lib)) present.add(lib);
        }
        return present;
    }

    private static List<String> evidenceFromApkSplits(Context context) {
        if (context == null) return java.util.Collections.emptyList();
        ApplicationInfo info = context.getApplicationInfo();
        if (info == null) return java.util.Collections.emptyList();

        List<String> paths = new ArrayList<>();
        if (info.sourceDir != null) paths.add(info.sourceDir);
        if (info.splitSourceDirs != null) paths.addAll(Arrays.asList(info.splitSourceDirs));

        Set<String> matches = new HashSet<>();
        for (String path : paths) {
            if (path == null || !path.endsWith(".apk")) continue;
            try (ZipFile apk = new ZipFile(path)) {
                Enumeration<? extends ZipEntry> entries = apk.entries();
                while (entries.hasMoreElements()) {
                    String name = entries.nextElement().getName();
                    if (!name.startsWith("lib/") || !name.endsWith(".so")) continue;
                    int slash = name.lastIndexOf('/');
                    String basename = slash >= 0 ? name.substring(slash + 1) : name;
                    if (LIB_TO_ENGINE.containsKey(basename)) matches.add(basename);
                }
            } catch (Throwable ignored) {
                // A missing/unreadable optional split must not break target startup.
            }
        }

        List<String> ordered = new ArrayList<>();
        for (String lib : LIB_TO_ENGINE.keySet()) {
            if (matches.contains(lib)) ordered.add(lib);
        }
        return ordered;
    }

    private static List<String> evidenceFromProcessMaps() {
        File maps = new File("/proc/self/maps");
        if (!maps.canRead()) return java.util.Collections.emptyList();
        Set<String> mappedLibraries = new HashSet<>();
        try (BufferedReader reader = new BufferedReader(new FileReader(maps))) {
            String line;
            while ((line = reader.readLine()) != null) {
                for (String lib : LIB_TO_ENGINE.keySet()) {
                    if (line.endsWith("/" + lib)) mappedLibraries.add(lib);
                }
            }
        } catch (Throwable ignored) {
            return java.util.Collections.emptyList();
        }
        List<String> ordered = new ArrayList<>();
        for (String lib : LIB_TO_ENGINE.keySet()) {
            if (mappedLibraries.contains(lib)) ordered.add(lib);
        }
        return ordered;
    }
}
