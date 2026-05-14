# LSPosed resolves this entry class by string from META-INF/xposed/java_init.list.
-keep class com.jordan.rogue.recovery.ModuleEntry { *; }

# libxposed framework callbacks invoked reflectively by the loader.
-keepclassmembers class com.jordan.rogue.recovery.ModuleEntry {
    public void onModuleLoaded(...);
    public void onPackageLoaded(...);
    public void onPackageReady(...);
    public void onSystemServerStarting(...);
}

# RegisterNatives binds these method names as strings in JNI_OnLoad (template_native.cpp).
# If you rename any of them, update the native method table too.
-keep class com.jordan.rogue.recovery.NativeBridge { *; }
-keep class com.jordan.rogue.recovery.NativeUtils { *; }

# Keep the feature-flag key constants readable in case a downstream tool inspects them.
-keepclassmembers class com.jordan.rogue.recovery.FeatureRegistry {
    public static final java.lang.String KEY_*;
}

# libxposed API is compileOnly, but R8 may still encounter references via the base class.
-dontwarn io.github.libxposed.api.**
-keep class io.github.libxposed.api.** { *; }

# Keep META-INF/xposed/* filenames untouched.
-adaptresourcefilenames **.list,**.prop
