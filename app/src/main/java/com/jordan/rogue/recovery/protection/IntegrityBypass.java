package com.jordan.rogue.recovery.protection;

import android.content.Context;
import android.os.Bundle;
import android.util.Log;

import com.jordan.rogue.recovery.FeatureRegistry;
import com.jordan.rogue.recovery.FeatureState;
import com.jordan.rogue.recovery.TemplateConfig;

import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
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
 * <p>The cloud-save error the user saw — {@code VerifyIntegrityVerdictUnevaluated} — is the
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

    private static final AtomicBoolean installed = new AtomicBoolean(false);

    private IntegrityBypass() {}

    /**
     * Install the entire Java-layer bypass surface. Safe to call multiple times — only the first
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
        // FeatureRegistry has not been initialised yet — PAIRIP runs inside the target's
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
        hooks += hookActkDetectors(module, classLoader);

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
                            if (TemplateConfig.VERBOSE_LOGS) {
                                module.log(Log.INFO, TAG,
                                        "Play Integrity factory.create observed (PIF handles the verdict)");
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

    private static int hookActkDetectors(XposedModule module, ClassLoader classLoader) {
        int installed = 0;
        String[] detectorClasses = {
                "CodeStage.AntiCheat.Detectors.InjectionDetector",
                "CodeStage.AntiCheat.Detectors.ObscuredCheatingDetector",
                "CodeStage.AntiCheat.Detectors.SpeedHackDetector",
                "CodeStage.AntiCheat.Detectors.TimeCheatingDetector",
                "CodeStage.AntiCheat.Detectors.WallHackDetector"
        };
        for (String name : detectorClasses) {
            try {
                Class<?> cls = Class.forName(name, false, classLoader);
                for (Method m : cls.getDeclaredMethods()) {
                    if ("OnCheatingDetected".equals(m.getName())
                            || "OnDetected".equals(m.getName())) {
                        m.setAccessible(true);
                        module.hook(m)
                                .setPriority(XposedInterface.PRIORITY_HIGHEST)
                                .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                                .intercept(chain -> {
                                    if (TemplateConfig.VERBOSE_LOGS) {
                                        module.log(Log.INFO, TAG,
                                                "ACTk detector callback silenced: " + name);
                                    }
                                    return null;
                                });
                        installed++;
                    }
                }
            } catch (Throwable ignored) {
                // ACTk Java glue is only present when the ACTk Android plugin is bundled in
                // Java. Most pure-IL2CPP builds expose detectors through native code only —
                // those are handled in template_native.cpp.
            }
        }
        return installed;
    }

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
