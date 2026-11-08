package com.template.lsposed.engine;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.zip.ZipFile;

/** Diagnostic evidence only. A library name never authorizes a hook or proves engine readiness. */
public final class EngineDetector {
    public enum Engine {
        UNITY, UNREAL, COCOS2DX, GODOT, FLUTTER, REACT_NATIVE, XAMARIN,
        MONO, LUA, JAVASCRIPT, JAVA, NATIVE, UNKNOWN
    }

    private static final Map<String, Engine> LIBRARIES = new LinkedHashMap<>();
    static {
        LIBRARIES.put("libil2cpp.so", Engine.UNITY);
        LIBRARIES.put("libunity.so", Engine.UNITY); // Unity can also use Mono; do not infer IL2CPP.
        LIBRARIES.put("libUE4.so", Engine.UNREAL);
        LIBRARIES.put("libUnreal.so", Engine.UNREAL);
        LIBRARIES.put("libcocos2djs.so", Engine.COCOS2DX);
        LIBRARIES.put("libcocos2dcpp.so", Engine.COCOS2DX);
        LIBRARIES.put("libgodot_android.so", Engine.GODOT);
        LIBRARIES.put("libflutter.so", Engine.FLUTTER);
        LIBRARIES.put("libreactnative.so", Engine.REACT_NATIVE);
        LIBRARIES.put("libreactnativejni.so", Engine.REACT_NATIVE);
        LIBRARIES.put("libmonodroid.so", Engine.XAMARIN);
        LIBRARIES.put("libmonosgen-2.0.so", Engine.MONO);
        LIBRARIES.put("libmonobdwgc-2.0.so", Engine.MONO);
        LIBRARIES.put("liblua.so", Engine.LUA);
        LIBRARIES.put("libluajit.so", Engine.LUA);
        // A JavaScript VM by itself does not establish React Native.
        LIBRARIES.put("libjsc.so", Engine.JAVASCRIPT);
        LIBRARIES.put("libhermes.so", Engine.JAVASCRIPT);
    }

    private EngineDetector() {}

    private static final class Scan {
        final Set<String> packaged = new HashSet<>();
        final Set<String> evidence = new HashSet<>();
        boolean inspectedApk;
        boolean unreadableApk;
    }

    public static Engine detect(Context context) {
        Scan scan = scan(context);
        for (Map.Entry<String, Engine> marker : LIBRARIES.entrySet())
            if (scan.evidence.contains(marker.getKey())) return marker.getValue();
        if (!scan.packaged.isEmpty()) return Engine.NATIVE;
        return scan.inspectedApk && !scan.unreadableApk ? Engine.JAVA : Engine.UNKNOWN;
    }

    public static List<String> evidence(Context context) {
        Scan scan = scan(context);
        List<String> ordered = new ArrayList<>();
        for (String name : LIBRARIES.keySet()) if (scan.evidence.contains(name)) ordered.add(name);
        return ordered;
    }

    public static List<String> evidenceFromNativeLibraryDir(Context context) {
        List<String> result = new ArrayList<>();
        if (context == null) return result;
        ApplicationInfo info = context.getApplicationInfo();
        String[] files = info != null && info.nativeLibraryDir != null ? new File(info.nativeLibraryDir).list() : null;
        if (files != null) for (String file : files) if (file.endsWith(".so")) result.add(file);
        return result;
    }

    private static Scan scan(Context context) {
        Scan out = new Scan();
        out.packaged.addAll(evidenceFromNativeLibraryDir(context));
        ApplicationInfo info = context == null ? null : context.getApplicationInfo();
        List<String> paths = new ArrayList<>();
        if (info != null) {
            if (info.sourceDir != null) paths.add(info.sourceDir);
            if (info.splitSourceDirs != null) java.util.Collections.addAll(paths, info.splitSourceDirs);
        }
        for (String path : paths) {
            if (path == null) continue;
            try (ZipFile apk = new ZipFile(path)) {
                out.inspectedApk = true;
                var entries = apk.entries();
                while (entries.hasMoreElements()) {
                    String name = entries.nextElement().getName();
                    if (name.startsWith("lib/") && name.endsWith(".so"))
                        out.packaged.add(name.substring(name.lastIndexOf('/') + 1));
                }
            } catch (Exception ignored) { out.unreadableApk = true; }
        }
        out.evidence.addAll(out.packaged);
        try (BufferedReader maps = new BufferedReader(new FileReader("/proc/self/maps"))) {
            String line;
            while ((line = maps.readLine()) != null) {
                if (line.endsWith(" (deleted)")) line = line.substring(0, line.length() - 10);
                for (String name : LIBRARIES.keySet()) if (line.endsWith("/" + name)) out.evidence.add(name);
            }
        } catch (Exception ignored) { /* Packaged evidence still works when maps is unavailable. */ }
        return out;
    }
}
