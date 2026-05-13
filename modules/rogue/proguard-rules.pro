# LSPosed resolves this entry class by string from META-INF/xposed/java_init.list.
-keep class com.devin.lsposed.rogue.ModuleEntry { *; }

# libxposed framework callbacks invoked reflectively by the loader.
-keepclassmembers class com.devin.lsposed.rogue.ModuleEntry {
    public void onModuleLoaded(...);
    public void onPackageLoaded(...);
    public void onPackageReady(...);
    public void onSystemServerStarting(...);
}

# Keep hook classes; ModuleEntry references them by name.
-keep class com.devin.lsposed.rogue.hooks.** { *; }

# Keep the feature-flag key constants readable in case a downstream tool inspects them.
-keepclassmembers class com.devin.lsposed.rogue.FeatureRegistry {
    public static final java.lang.String KEY_*;
}

# libxposed API is compileOnly, but R8 may still encounter references via the base class.
-dontwarn io.github.libxposed.api.**
-keep class io.github.libxposed.api.** { *; }

# Keep META-INF/xposed/* filenames untouched.
-adaptresourcefilenames **.list,**.prop
