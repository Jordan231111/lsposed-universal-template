plugins {
    alias(libs.plugins.android.application)
}

android {
    namespace = "com.devin.lsposed.rogue"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.devin.lsposed.rogue"
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"

        // Java-only LSPosed module: no native code, so the APK is ABI-neutral and
        // works on arm64-v8a, x86_64, and any other ABI LSPosed/Zygisk runs on.
        ndk {
            // The empty filter list keeps Gradle from forcing a per-ABI split and
            // produces a single universal APK. We don't ship any .so anyway.
        }
    }

    // Optional release signing driven by environment variables. When any required var is
    // missing, release builds fall back to the debug keystore so local builds still install.
    //   TEMPLATE_KS_PATH       absolute path to the keystore file
    //   TEMPLATE_KS_PASS       keystore password
    //   TEMPLATE_KEY_ALIAS     key alias inside the keystore
    //   TEMPLATE_KEY_PASS      key password
    signingConfigs {
        create("releaseEnv") {
            val ksPath = System.getenv("TEMPLATE_KS_PATH")
            val ksPass = System.getenv("TEMPLATE_KS_PASS")
            val alias = System.getenv("TEMPLATE_KEY_ALIAS")
            val keyPass = System.getenv("TEMPLATE_KEY_PASS")
            if (!ksPath.isNullOrBlank() && !ksPass.isNullOrBlank()
                    && !alias.isNullOrBlank() && !keyPass.isNullOrBlank()) {
                storeFile = file(ksPath)
                storePassword = ksPass
                keyAlias = alias
                keyPassword = keyPass
            }
        }
    }

    buildTypes {
        debug {
            isMinifyEnabled = false
            buildConfigField("boolean", "VERBOSE_LOGS", "true")
        }
        release {
            isMinifyEnabled = true
            isShrinkResources = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            buildConfigField("boolean", "VERBOSE_LOGS", "false")

            val envSigning = signingConfigs.getByName("releaseEnv")
            signingConfig = if (envSigning.storeFile != null) envSigning else signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        buildConfig = true
    }

    packaging {
        resources {
            merges += "META-INF/xposed/*"
        }
    }
}

dependencies {
    compileOnly(libs.libxposed.api)
    compileOnly(libs.androidx.annotation)
}
