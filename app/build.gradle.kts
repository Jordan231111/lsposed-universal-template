plugins {
    alias(libs.plugins.android.application)
}

android {
    namespace = "com.template.lsposed"
    compileSdk = 37
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "com.template.lsposed"
        minSdk = 26
        targetSdk = 37
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
            // Release-only Android res/ optimization. It does not strip assets, META-INF entries,
            // or native libraries; every resource in this template is referenced statically.
            isShrinkResources = true
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

    // Two framework targets, so each artifact declares exactly one API level and one entry set:
    //   lsposed  -> modern libxposed API 102 (META-INF/xposed/java_init.list -> ModuleEntry). Root.
    //   lspatch  -> classic Xposed API 93   (assets/xposed_init -> LSPatchEntry). Non-root; LSPatch is API 93.
    // Per-flavor module.prop lives in src/<flavor>/resources/... ; the classic assets/xposed_init lives
    // only in src/lspatch/. The manifest's xposedminversion is injected per flavor below.
    flavorDimensions += "framework"
    productFlavors {
        create("lsposed") {
            dimension = "framework"
            manifestPlaceholders["xposedMinVersion"] = "102"
        }
        create("lspatch") {
            dimension = "framework"
            manifestPlaceholders["xposedMinVersion"] = "93"
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "4.1.2"
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

    lint {
        // ShadowHook intentionally supports ARM/ARM64 only; Java-only users can disable native loading.
        disable += "ChromeOsAbiSupport"
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
    // Keep the two framework APIs out of the opposite flavor. In particular, an API-102 modern
    // module must not call the legacy API, while LSPatch still requires the classic entry surface.
    add("lsposedCompileOnly", libs.libxposed.api)
    add("lsposedCompileOnly", libs.androidx.annotation)
    // API 82 is the final official classic compile artifact. LSPatch implements the compatible
    // classic API level 93 at runtime; the framework supplies these classes, so do not package them.
    add("lspatchCompileOnly", "de.robv.android.xposed:api:82")
    implementation(libs.shadowhook)
}
