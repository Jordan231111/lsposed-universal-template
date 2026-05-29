package com.jordan.rogue.recovery.protection;

import android.content.Context;
import android.os.Bundle;
import android.util.Log;

import com.jordan.rogue.recovery.FeatureRegistry;
import com.jordan.rogue.recovery.FeatureState;
import com.jordan.rogue.recovery.TemplateConfig;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.Collections;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;

import io.github.libxposed.api.XposedInterface;
import io.github.libxposed.api.XposedModule;

/**
 * Java-layer bypass for the protection surface that ships inside Rogue with the Dead 3.11.1.
 *
 * <p>The base APK is wrapped by Google PAIRIP (Play Application Integrity Protection). PAIRIP runs
 * two checks at process startup:
 *
 * <ul>
 *   <li>{@code com.pairip.SignatureCheck.verifyIntegrity(Context)} confirms the install signature
 *       matches one of the developer's known hashes. Any modification to the APK or to the
 *       installer chain trips this check.</li>
 *   <li>{@code com.pairip.licensecheck.LicenseClient.checkLicense(Context)} binds to the Play
 *       Store licensing service. On rooted emulators the bind frequently fails because Play Store
 *       is missing, in a restricted profile, or the user did not purchase the app from the linked
 *       account.</li>
 * </ul>
 *
 * <p>The cloud-save error the user saw - {@code VerifyIntegrityVerdictUnevaluated} - is the
 * Play Integrity verdict computed by Google when the request is sent from a process Google
 * cannot evaluate. The integrity token arrives at the PlayFab CloudScript backend with an
 * {@code UNEVALUATED} body and the server rejects the cloud-save registration.</p>
 *
 * <p>This class neutralises every Java-side hook that an emulator can realistically fight. The
 * native-side bypass for the IL2CPP {@code ServerManager} runs through {@link
 * com.jordan.rogue.recovery.NativeBridge} on a different code path and is layered on top of these
 * hooks.</p>
 */
public final class IntegrityBypass {
    private static final String TAG = TemplateConfig.LOG_TAG;
    private static final long CLOUD_PROJECT_NUMBER = 814383916740L;

    private static final AtomicBoolean installed = new AtomicBoolean(false);
    private static final Set<String> hookedConcreteManagers =
            Collections.newSetFromMap(new ConcurrentHashMap<>());

    private IntegrityBypass() {}

    /**
     * Install the entire Java-layer bypass surface. Safe to call multiple times - only the first
     * call performs any work. Individual hooks degrade independently if a target class is missing
     * (older APK build, future refactor, etc.).
     *
     * @param module       the active libxposed module that owns the hook lifecycle
     * @param classLoader  the target process classloader; comes from
     *                     {@link XposedModule.PackageLoadedParam#getClassLoader()}
     */
    public static synchronized void install(XposedModule module, ClassLoader classLoader) {
        if (module == null || classLoader == null) return;
        if (!installed.compareAndSet(false, true)) return;

        // The compile-time flag is the source of truth at this entry point because
        // FeatureRegistry has not been initialised yet - PAIRIP runs inside the target's
        // Application static initialiser, which is earlier than our onApplicationContextReady
        // callback. Once the registry is up, hooks honour the live KEY_INTEGRITY_BYPASS toggle
        // via the inner method gate in each install* helper.
        if (!TemplateConfig.ENABLE_INTEGRITY_BYPASS) {
            if (TemplateConfig.VERBOSE_LOGS) {
                module.log(Log.INFO, TAG,
                        "IntegrityBypass disabled by TemplateConfig; skipping install");
            }
            installed.set(false);
            return;
        }

        int hooks = 0;
        hooks += hookPairipSignatureCheck(module, classLoader);
        hooks += hookPairipLicenseClient(module, classLoader);
        hooks += hookPairipLicenseResponseHelper(module, classLoader);
        hooks += hookPlayIntegrityFactory(module, classLoader);
        hooks += hookPlayIntegrityManager(module, classLoader);
        hooks += hookPlayIntegrityResponse(module, classLoader);
        // Anti-Cheat Toolkit detectors are pure IL2CPP/C# types (CodeStage.AntiCheat.Detectors.*),
        // never Java classes, so a Java-side Class.forName/hook can never resolve them. ACTk is
        // neutralised natively in template_native.cpp via the shared OnCheatingDetected report sink
        // (gated on the actk_bypass toggle). No Java hook is attempted here by design.

        FeatureState.setLastMessage("IntegrityBypass installed " + hooks + " java hooks");
        if (TemplateConfig.VERBOSE_LOGS) {
            module.log(Log.INFO, TAG, "IntegrityBypass installed " + hooks + " java hooks");
        }
    }

    private static int hookPairipSignatureCheck(XposedModule module, ClassLoader classLoader) {
        int installed = 0;
        try {
            Class<?> cls = Class.forName("com.pairip.SignatureCheck", false, classLoader);

            Method verifyIntegrity = cls.getDeclaredMethod("verifyIntegrity", Context.class);
            verifyIntegrity.setAccessible(true);
            module.hook(verifyIntegrity)
                    .setPriority(XposedInterface.PRIORITY_HIGHEST)
                    .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                    .intercept(chain -> {
                        FeatureState.setLastMessage("PAIRIP SignatureCheck bypassed");
                        if (TemplateConfig.VERBOSE_LOGS) {
                            module.log(Log.INFO, TAG, "PAIRIP SignatureCheck.verifyIntegrity bypassed");
                        }
                        return null;
                    });
            installed++;

            Method verifySignatureMatches = cls.getDeclaredMethod("verifySignatureMatches", String.class);
            verifySignatureMatches.setAccessible(true);
            module.hook(verifySignatureMatches)
                    .setPriority(XposedInterface.PRIORITY_HIGHEST)
                    .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                    .intercept(chain -> Boolean.TRUE);
            installed++;
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                module.log(Log.WARN, TAG, "Failed to hook com.pairip.SignatureCheck: " + t);
            }
        }
        return installed;
    }

    private static int hookPairipLicenseClient(XposedModule module, ClassLoader classLoader) {
        int installed = 0;
        try {
            Class<?> cls = Class.forName("com.pairip.licensecheck.LicenseClient", false, classLoader);
            Method checkLicense = cls.getDeclaredMethod("checkLicense", Context.class);
            checkLicense.setAccessible(true);
            module.hook(checkLicense)
                    .setPriority(XposedInterface.PRIORITY_HIGHEST)
                    .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                    .intercept(chain -> {
                        FeatureState.setLastMessage("PAIRIP LicenseClient.checkLicense bypassed");
                        if (TemplateConfig.VERBOSE_LOGS) {
                            module.log(Log.INFO, TAG, "PAIRIP LicenseClient.checkLicense bypassed");
                        }
                        return null;
                    });
            installed++;

            try {
                Method initialize = cls.getDeclaredMethod("initializeLicenseCheck");
                initialize.setAccessible(true);
                module.hook(initialize)
                        .setPriority(XposedInterface.PRIORITY_HIGHEST)
                        .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                        .intercept(chain -> {
                            if (TemplateConfig.VERBOSE_LOGS) {
                                module.log(Log.INFO, TAG, "PAIRIP initializeLicenseCheck bypassed");
                            }
                            return null;
                        });
                installed++;
            } catch (NoSuchMethodException ignored) { }
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                module.log(Log.WARN, TAG, "Failed to hook com.pairip.licensecheck.LicenseClient: " + t);
            }
        }
        return installed;
    }

    private static int hookPairipLicenseResponseHelper(XposedModule module, ClassLoader classLoader) {
        int installed = 0;
        try {
            Class<?> cls = Class.forName("com.pairip.licensecheck.LicenseResponseHelper", false, classLoader);
            for (Method m : cls.getDeclaredMethods()) {
                if ("validateResponse".equals(m.getName())) {
                    m.setAccessible(true);
                    module.hook(m)
                            .setPriority(XposedInterface.PRIORITY_HIGHEST)
                            .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                            .intercept(chain -> {
                                if (TemplateConfig.VERBOSE_LOGS) {
                                    module.log(Log.INFO, TAG, "PAIRIP validateResponse bypassed");
                                }
                                return null;
                            });
                    installed++;
                }
            }
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                module.log(Log.WARN, TAG, "Failed to hook PAIRIP LicenseResponseHelper: " + t);
            }
        }
        return installed;
    }

    private static int hookPlayIntegrityFactory(XposedModule module, ClassLoader classLoader) {
        int installed = 0;
        try {
            Class<?> factoryCls = Class.forName(
                    "com.google.android.play.core.integrity.IntegrityManagerFactory",
                    false, classLoader);
            for (Method m : factoryCls.getDeclaredMethods()) {
                if (!"create".equals(m.getName())) continue;
                m.setAccessible(true);
                module.hook(m)
                        .setPriority(XposedInterface.PRIORITY_LOWEST)
                        .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                        .intercept(chain -> {
                            Object result = chain.proceed();
                            hookConcretePlayIntegrityManager(module, classLoader, result);
                            if (TemplateConfig.VERBOSE_LOGS) {
                                module.log(Log.INFO, TAG,
                                        "Play Integrity factory.create observed: "
                                                + classNameOf(result));
                            }
                            return result;
                        });
                installed++;
            }
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                module.log(Log.WARN, TAG, "Failed to hook IntegrityManagerFactory: " + t);
            }
        }
        return installed;
    }

    private static void hookConcretePlayIntegrityManager(
            XposedModule module, ClassLoader classLoader, Object manager) {
        if (manager == null) return;
        Class<?> cls = manager.getClass();
        if (!hookedConcreteManagers.add(cls.getName())) return;

        int installed = 0;
        for (Class<?> current = cls; current != null && current != Object.class;
                current = current.getSuperclass()) {
            for (Method m : current.getDeclaredMethods()) {
                if (!"requestIntegrityToken".equals(m.getName())
                        || m.getParameterTypes().length != 1) {
                    continue;
                }
                try {
                    m.setAccessible(true);
                    module.hook(m)
                            .setPriority(XposedInterface.PRIORITY_HIGHEST)
                            .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                            .intercept(chain -> {
                                Object original = chain.getArg(0);
                                Object patched = buildIntegrityRequestWithCloudProject(
                                        module, classLoader, original);
                                if (patched != original) {
                                    return chain.proceed(new Object[] { patched });
                                }
                                return chain.proceed();
                            });
                    installed++;
                } catch (Throwable t) {
                    if (TemplateConfig.VERBOSE_LOGS) {
                        module.log(Log.WARN, TAG,
                                "Failed concrete IntegrityManager hook "
                                        + current.getName() + "." + m.getName() + ": " + t);
                    }
                }
            }
        }

        if (TemplateConfig.VERBOSE_LOGS) {
            module.log(Log.INFO, TAG,
                    "Concrete Play Integrity manager hooks for " + cls.getName()
                            + ": " + installed);
        }
        if (installed == 0) {
            hookedConcreteManagers.remove(cls.getName());
        }
    }

    private static Object buildIntegrityRequestWithCloudProject(
            XposedModule module, ClassLoader classLoader, Object original) {
        if (original == null || !TemplateConfig.ENABLE_SERVER_INTEGRITY_BYPASS) {
            return original;
        }
        try {
            Long current = asLong(invokeNoArg(original, "cloudProjectNumber", "getCloudProjectNumber"));
            if (current != null && current == CLOUD_PROJECT_NUMBER) {
                return original;
            }

            String nonce = asString(invokeNoArg(original, "nonce", "getNonce"));
            if (nonce == null || nonce.isEmpty()) {
                return patchConcreteIntegrityRequest(module, original) ? original : original;
            }

            Class<?> requestCls = Class.forName(
                    "com.google.android.play.core.integrity.IntegrityTokenRequest",
                    false, classLoader);
            Method builderMethod = requestCls.getDeclaredMethod("builder");
            builderMethod.setAccessible(true);
            Object builder = builderMethod.invoke(null);
            invokeBuilder(builder, "setNonce", nonce);
            invokeBuilder(builder, "setCloudProjectNumber", CLOUD_PROJECT_NUMBER);

            Object network = invokeNoArg(original, "network", "getNetwork");
            if (network != null) {
                invokeBuilder(builder, "setNetwork", network);
            }

            Object patched = invokeNoArg(builder, "build");
            if (patched != null) {
                if (TemplateConfig.VERBOSE_LOGS) {
                    module.log(Log.INFO, TAG,
                            "Play Integrity request patched: project=" + CLOUD_PROJECT_NUMBER
                                    + " nonceLen=" + nonce.length()
                                    + " oldProject=" + current);
                }
                return patched;
            }
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                module.log(Log.WARN, TAG,
                        "Builder patch for Play Integrity request failed: " + t);
            }
        }

        patchConcreteIntegrityRequest(module, original);
        return original;
    }

    private static boolean patchConcreteIntegrityRequest(XposedModule module, Object request) {
        if (request == null) return false;
        try {
            for (Class<?> current = request.getClass(); current != null && current != Object.class;
                    current = current.getSuperclass()) {
                for (Field field : current.getDeclaredFields()) {
                    Class<?> type = field.getType();
                    if (type != Long.class && type != Long.TYPE) continue;
                    field.setAccessible(true);
                    if (type == Long.TYPE) {
                        field.setLong(request, CLOUD_PROJECT_NUMBER);
                    } else {
                        field.set(request, Long.valueOf(CLOUD_PROJECT_NUMBER));
                    }
                    if (TemplateConfig.VERBOSE_LOGS) {
                        module.log(Log.INFO, TAG,
                                "Play Integrity request field patched: "
                                        + current.getName() + "." + field.getName());
                    }
                    return true;
                }
            }
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                module.log(Log.WARN, TAG,
                        "Field patch for Play Integrity request failed: " + t);
            }
        }
        return false;
    }

    private static Object invokeNoArg(Object target, String... methodNames) throws ReflectiveOperationException {
        if (target == null) return null;
        Class<?> cls = target instanceof Class<?> ? (Class<?>) target : target.getClass();
        Object receiver = target instanceof Class<?> ? null : target;
        for (String name : methodNames) {
            try {
                Method method = cls.getMethod(name);
                method.setAccessible(true);
                return method.invoke(receiver);
            } catch (NoSuchMethodException ignored) {
                try {
                    Method method = cls.getDeclaredMethod(name);
                    method.setAccessible(true);
                    return method.invoke(receiver);
                } catch (NoSuchMethodException ignoredAgain) {
                    // Try the next candidate name.
                }
            }
        }
        return null;
    }

    private static void invokeBuilder(Object builder, String methodName, Object value)
            throws ReflectiveOperationException {
        for (Method method : builder.getClass().getMethods()) {
            if (!methodName.equals(method.getName()) || method.getParameterTypes().length != 1) {
                continue;
            }
            method.setAccessible(true);
            method.invoke(builder, value);
            return;
        }
        for (Method method : builder.getClass().getDeclaredMethods()) {
            if (!methodName.equals(method.getName()) || method.getParameterTypes().length != 1) {
                continue;
            }
            method.setAccessible(true);
            method.invoke(builder, value);
            return;
        }
        throw new NoSuchMethodException(builder.getClass().getName() + "." + methodName);
    }

    private static Long asLong(Object value) {
        return value instanceof Number ? ((Number) value).longValue() : null;
    }

    private static String asString(Object value) {
        return value instanceof String ? (String) value : null;
    }

    private static String classNameOf(Object object) {
        return object != null ? object.getClass().getName() : "<null>";
    }

    private static int hookPlayIntegrityManager(XposedModule module, ClassLoader classLoader) {
        int installed = 0;
        try {
            Class<?> managerCls = Class.forName(
                    "com.google.android.play.core.integrity.IntegrityManager",
                    false, classLoader);
            for (Method m : managerCls.getDeclaredMethods()) {
                if (!"requestIntegrityToken".equals(m.getName())) continue;
                m.setAccessible(true);
                module.hook(m)
                        .setPriority(XposedInterface.PRIORITY_LOWEST)
                        .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                        .intercept(chain -> {
                            Object result = chain.proceed();
                            if (TemplateConfig.VERBOSE_LOGS) {
                                module.log(Log.INFO, TAG,
                                        "Play Integrity requestIntegrityToken observed");
                            }
                            return result;
                        });
                installed++;
            }
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                module.log(Log.WARN, TAG, "Failed to hook Play Integrity Manager: " + t);
            }
        }
        return installed;
    }

    private static int hookPlayIntegrityResponse(XposedModule module, ClassLoader classLoader) {
        int installed = 0;
        try {
            Class<?> responseCls = Class.forName(
                    "com.google.android.play.core.integrity.IntegrityTokenResponse",
                    false, classLoader);
            for (Method m : responseCls.getDeclaredMethods()) {
                if (!"token".equals(m.getName())) continue;
                m.setAccessible(true);
                module.hook(m)
                        .setPriority(XposedInterface.PRIORITY_LOWEST)
                        .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                        .intercept(chain -> {
                            Object original = chain.proceed();
                            if (TemplateConfig.VERBOSE_LOGS) {
                                module.log(Log.INFO, TAG,
                                        "Play Integrity token() observed; token len="
                                                + (original instanceof String
                                                        ? ((String) original).length() : -1));
                            }
                            return original;
                        });
                installed++;
            }
        } catch (Throwable t) {
            if (TemplateConfig.VERBOSE_LOGS) {
                module.log(Log.WARN, TAG, "Failed to hook IntegrityTokenResponse: " + t);
            }
        }
        return installed;
    }

    // NOTE: a Java-side ACTk detector hook used to live here. It was removed because
    // CodeStage.AntiCheat.Detectors.* are IL2CPP/C# types, never loadable as Java classes, so
    // Class.forName always threw and the method installed zero hooks. ACTk is now neutralised
    // natively (template_native.cpp: proxy_actk_on_cheating_detected on the shared
    // ACTkDetectorBase<T>.OnCheatingDetected sink, gated on the actk_bypass toggle).

    /**
     * Construct a synthetic, "passing" PAIRIP license payload {@link Bundle}. Kept available for
     * future use if the dev decides to hook into {@code LicenseResponseHelper.getRepeatedCheckMetadata}
     * with a fake bundle instead of dropping the call entirely.
     */
    @SuppressWarnings("unused")
    public static Bundle buildSyntheticLicensePayload() {
        Bundle b = new Bundle();
        b.putString("LICENSED", "true");
        return b;
    }

    /** Returns the runtime class of a private inner class without crashing if it disappears. */
    @SuppressWarnings("unused")
    private static Class<?> findInnerClass(Class<?> outer, String simpleName) {
        for (Class<?> c : outer.getDeclaredClasses()) {
            if (c.getSimpleName().equals(simpleName)) return c;
        }
        return null;
    }

    /** Reflectively pick a public no-arg constructor; used by future stub builders. */
    @SuppressWarnings("unused")
    private static Object newInstanceOrNull(Class<?> cls) {
        try {
            Constructor<?> ctor = cls.getDeclaredConstructor();
            ctor.setAccessible(true);
            return ctor.newInstance();
        } catch (Throwable t) {
            return null;
        }
    }
}
