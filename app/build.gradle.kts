plugins {
    alias(libs.plugins.android.application)
}

android {
    namespace = "com.template.lsposed"
    compileSdk = 37
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = "com.template.lsposed"
        // Match Vector/LSPatch's Android 9 runtime floor; compileSdk is a build-time requirement.
        minSdk = 28
        targetSdk = 37
        versionCode = 2
        versionName = "0.2.0"

        // ShadowHook supports arm32 and arm64 only. Use an arm64 emulator/device for native hooks.
        // A Java-only fork must also remove the native build/dependency and ABI filters.
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static")
                cppFlags += listOf("-std=c++20", "-fno-rtti", "-fexceptions", "-fvisibility=hidden")
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
    // Vector 2.2 and LSPatch 1.2 provide libxposed API 102 at runtime. Keep the API out of the APK.
    compileOnly(libs.libxposed.api)
    compileOnly(libs.androidx.annotation)
    implementation(libs.shadowhook)
}
