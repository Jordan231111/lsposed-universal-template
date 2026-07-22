package com.devin.lsposed.rogue.hooks;

import android.util.Log;

import com.devin.lsposed.rogue.FeatureRegistry;
import com.devin.lsposed.rogue.FeatureState;
import com.devin.lsposed.rogue.TemplateConfig;

import java.lang.reflect.Method;

import io.github.libxposed.api.XposedInterface;
import io.github.libxposed.api.XposedModule;

/**
 * No-ops Google PAIRIP (anti-tampering / integrity-attestation framework that Google bundles
 * into Play-distributed APKs). Both target apps include {@code com.pairip.application.Application}
 * which runs:
 *
 * <ol>
 *   <li>{@code com.pairip.SignatureCheck.verifyIntegrity(context)} - SHA-256 hash of the APK's
 *       signing cert, compared against a hardcoded constant. If the APK has been resigned (e.g.
 *       repackaging tools), it throws and aborts startup.</li>
 *   <li>{@code com.pairip.StartupLauncher.launch()} - kicks off a license/integrity attestation
 *       chain via {@code libpairipcore.so}, which can short-circuit the app when running in an
 *       unusual environment (some emulators, rooted devices, modified userdata).</li>
 *   <li>{@code com.pairip.licensecheck.LicenseClient.processResponse(...)} - reads the Google
 *       Play licensing response and gates the app on it.</li>
 * </ol>
 *
 * <p>LSPosed instrumentation does not modify the APK on disk, so {@code verifyIntegrity} normally
 * passes; we still no-op it because some PAIRIP variants additionally call back into the native
 * VM for code-checksum validation and that can flag a hooked process. Hooking the License-check
 * paths gives the user a single-flag escape hatch if Google's response is unavailable
 * (offline / sideloaded device).</p>
 */
public final class PairipBypassHook {

    private PairipBypassHook() {}

    public static void install(XposedModule module, ClassLoader cl) {
        if (!FeatureRegistry.getBool(FeatureRegistry.KEY_PAIRIP_BYPASS)) return;
        hookSignatureCheck(module, cl);
        hookLicenseClient(module, cl);
    }

    private static void hookSignatureCheck(XposedModule module, ClassLoader cl) {
        try {
            Class<?> sigCheck = Class.forName("com.pairip.SignatureCheck", false, cl);
            for (Method m : sigCheck.getDeclaredMethods()) {
                if (!"verifyIntegrity".equals(m.getName())) continue;
                try {
                    module.hook(m)
                            .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                            .intercept(chain -> {
                                FeatureState.bumpPairipBypassHits();
                                if (TemplateConfig.VERBOSE_LOGS) {
                                    Log.i(TemplateConfig.LOG_TAG, "PAIRIP: SignatureCheck.verifyIntegrity bypassed");
                                }
                                return null;
                            });
                } catch (Throwable t) {
                    if (TemplateConfig.VERBOSE_LOGS) {
                        Log.w(TemplateConfig.LOG_TAG, "PAIRIP: failed to hook verifyIntegrity", t);
                    }
                }
            }
        } catch (Throwable ignored) {
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.i(TemplateConfig.LOG_TAG, "PAIRIP: SignatureCheck class not present");
            }
        }
    }

    private static void hookLicenseClient(XposedModule module, ClassLoader cl) {
        try {
            Class<?> lc = Class.forName("com.pairip.licensecheck.LicenseClient", false, cl);
            for (Method m : lc.getDeclaredMethods()) {
                String n = m.getName();
                // Common method names that gate the rest of the app.
                if (!("processResponse".equals(n)
                        || "dontAllow".equals(n)
                        || "applicationError".equals(n)
                        || "onError".equals(n))) {
                    continue;
                }
                try {
                    final String name = n;
                    module.hook(m)
                            .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                            .intercept(chain -> {
                                FeatureState.bumpPairipBypassHits();
                                if (TemplateConfig.VERBOSE_LOGS) {
                                    Log.i(TemplateConfig.LOG_TAG, "PAIRIP: LicenseClient." + name + " bypassed");
                                }
                                return null;
                            });
                } catch (Throwable t) {
                    if (TemplateConfig.VERBOSE_LOGS) {
                        Log.w(TemplateConfig.LOG_TAG, "PAIRIP: failed to hook LicenseClient." + n, t);
                    }
                }
            }
        } catch (Throwable ignored) {
            if (TemplateConfig.VERBOSE_LOGS) {
                Log.i(TemplateConfig.LOG_TAG, "PAIRIP: LicenseClient class not present");
            }
        }
    }
}
