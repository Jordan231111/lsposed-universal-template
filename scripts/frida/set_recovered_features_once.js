Java.perform(function () {
  let moduleFactory = null;
  Java.enumerateClassLoaders({
    onMatch(loader) {
      try {
        loader.loadClass("com.jordan.rogue.recovery.FeatureRegistry");
        moduleFactory = Java.ClassFactory.get(loader);
      } catch (_) {
      }
    },
    onComplete() {
    },
  });

  if (moduleFactory === null) {
    throw new Error("Rogue Recovery module class loader not found");
  }

  const ActivityThread = Java.use("android.app.ActivityThread");
  const app = ActivityThread.currentApplication();
  const context = app ? app.getApplicationContext() : null;

  const FeatureRegistry = moduleFactory.use("com.jordan.rogue.recovery.FeatureRegistry");
  if (context) FeatureRegistry.initialize(context);

  FeatureRegistry.setBool("enabled", true);
  FeatureRegistry.setBool("native_hooks", true);
  FeatureRegistry.setFloat("damage_multiplier", 5.0);
  FeatureRegistry.setFloat("defense_multiplier", 7.0);
  FeatureRegistry.setBool("god_mode", true);
  FeatureRegistry.setBool("free_shop", true);

  const NativeBridge = moduleFactory.use("com.jordan.rogue.recovery.NativeBridge");
  NativeBridge.syncFeatureState();
  console.log("synced recovered feature test values");
});
