plugins {
    alias(libs.plugins.android.application)
}

android {
    namespace = "com.jordan.rogue.recovery"
    compileSdk = 35
    ndkVersion = "25.2.9519653"

    defaultConfig {
        applicationId = "com.jordan.rogue.recovery"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"

        // ShadowHook supports arm32 and arm64 only. Use an arm64 emulator/device for native hooks.
        // Java-only LSPosed hooks still work after you disable native loading in TemplateConfig.
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static")
                cppFlags += listOf("-std=c++20", "-fno-rtti", "-fno-exceptions", "-fvisibility=hidden")
            }
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

    // Two product flavors that share native + most Java code but ship different module entry
    // points. The "lsposed" flavor is the original libxposed-api 101 build for Vector/LSPosed.
    // The "lspatch" flavor uses the classic IXposedHookLoadPackage entry under
    // `src/lspatch/...`, so LSPatch can wrap an existing target APK without a separate manager.
    // Different applicationIds let the two artifacts live side-by-side without conflict.
    flavorDimensions += "framework"
    productFlavors {
        create("lsposed") {
            dimension = "framework"
        }
        create("lspatch") {
            dimension = "framework"
            applicationIdSuffix = ".lspatch"
            versionNameSuffix = "-lspatch"
        }
    }

    sourceSets {
        getByName("lsposed") {
            // The libxposed-api 101 entry (ModuleEntry.java plus IntegrityBypass, which depends on
            // XposedModule) lives under src/lsposed/ and is compiled only into this flavor.
        }
        getByName("lspatch") {
            // The classic entry under src/lspatch/java/ delegates to NativeBridge for the heavy
            // lifting and compiles against the classic Xposed API alone.
            java.setSrcDirs(listOf("src/lspatch/java"))
            assets.setSrcDirs(listOf("src/lspatch/assets"))
        }
    }

    buildTypes {
        debug {
            isMinifyEnabled = false
            buildConfigField("boolean", "VERBOSE_LOGS", "true")
            externalNativeBuild {
                cmake {
                    arguments += listOf("-DTEMPLATE_VERBOSE_LOGS=1")
                }
            }
        }
        release {
            isMinifyEnabled = true
            isShrinkResources = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            buildConfigField("boolean", "VERBOSE_LOGS", "false")
            externalNativeBuild {
                cmake {
                    arguments += listOf("-DTEMPLATE_VERBOSE_LOGS=0")
                }
            }

            val envSigning = signingConfigs.getByName("releaseEnv")
            signingConfig = if (envSigning.storeFile != null) envSigning else signingConfigs.getByName("debug")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        prefab = true
        buildConfig = true
    }

    packaging {
        resources {
            merges += "META-INF/xposed/*"
        }
        jniLibs {
            useLegacyPackaging = false
            pickFirsts += listOf("**/libshadowhook.so", "**/libshadowhook_nothing.so")
        }
    }
}

dependencies {
    compileOnly(libs.androidx.annotation)
    implementation(libs.shadowhook)

    // libxposed-api 101 is consumed only by the default lsposed flavor.
    "lsposedCompileOnly"(libs.libxposed.api)

    // Classic Xposed API (Maven `de.robv.android.xposed:api:82`) is the entry point LSPatch uses.
    "lspatchCompileOnly"(libs.xposed.api.classic)
}
