import java.security.KeyStore
import java.io.File
import java.security.MessageDigest
import java.security.PrivateKey
import java.util.Locale
import org.gradle.api.provider.MapProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.Internal
import org.gradle.api.tasks.TaskAction
import org.gradle.work.DisableCachingByDefault

plugins {
    alias(libs.plugins.android.application)
}

val localTestSigning = providers.gradleProperty("template.localTestSigning")
    .map { it.toBooleanStrict() }.orElse(false).get()
val releaseInputs = listOf("TEMPLATE_KS_PATH", "TEMPLATE_KS_PASS", "TEMPLATE_KEY_ALIAS",
    "TEMPLATE_KEY_PASS", "TEMPLATE_CERT_SHA256").associateWith {
    providers.environmentVariable(it).orNull.orEmpty()
}
@DisableCachingByDefault(because = "Signing identity must be rechecked against local key material")
abstract class VerifyTemplateSigning : DefaultTask() {
    @get:Input abstract val localTest: Property<Boolean>
    @get:Internal abstract val credentials: MapProperty<String, String>

    @TaskAction fun verify() {
        val releaseInputs = credentials.get()
        if (localTest.get()) {
            if (releaseInputs.values.any { it.isNotBlank() })
                throw GradleException("Do not combine local test signing with release-signing environment variables")
            logger.lifecycle("Explicit local test signing: this is not a production distribution identity")
        } else {
            val missing = releaseInputs.filterValues { it.isBlank() }.keys
            if (missing.isNotEmpty()) throw GradleException(
                "Release signing requires ${missing.joinToString()}. For local QA only, use -Ptemplate.localTestSigning=true")
            val keyFile = File(releaseInputs.getValue("TEMPLATE_KS_PATH"))
            if (!keyFile.isAbsolute || !keyFile.isFile)
                throw GradleException("TEMPLATE_KS_PATH must name an existing absolute keystore path")
            val expected = releaseInputs.getValue("TEMPLATE_CERT_SHA256").trim().replace(":", "").lowercase(Locale.ROOT)
            if (!expected.matches(Regex("[0-9a-f]{64}"))) throw GradleException("Invalid TEMPLATE_CERT_SHA256")
            val keys = KeyStore.getInstance(keyFile, releaseInputs.getValue("TEMPLATE_KS_PASS").toCharArray())
            val alias = releaseInputs.getValue("TEMPLATE_KEY_ALIAS")
            if (keys.getKey(alias, releaseInputs.getValue("TEMPLATE_KEY_PASS").toCharArray()) !is PrivateKey)
                throw GradleException("The signing alias has no private key")
            val certificate = keys.getCertificate(alias) ?: throw GradleException("Signing certificate missing")
            val actual = MessageDigest.getInstance("SHA-256").digest(certificate.encoded)
                .joinToString("") { "%02x".format(it.toInt() and 0xff) }
            if (actual != expected) throw GradleException("Signing certificate does not match TEMPLATE_CERT_SHA256")
        }
    }
}
val verifyReleaseSigning = tasks.register<VerifyTemplateSigning>("verifyReleaseSigning") {
    group = "verification"
    description = "Require an explicit release identity or explicitly requested local test signing"
    localTest.set(localTestSigning)
    credentials.set(releaseInputs)
}
tasks.matching { it.name == "preReleaseBuild" }.configureEach { dependsOn(verifyReleaseSigning) }

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

    // Release identity is explicit and certificate-pinned. Local test signing requires an
    // explicit Gradle property; incomplete release configuration never selects another key.
    //   TEMPLATE_KS_PATH       absolute path to the keystore file
    //   TEMPLATE_KS_PASS       keystore password
    //   TEMPLATE_KEY_ALIAS     key alias inside the keystore
    //   TEMPLATE_KEY_PASS      key password
    //   TEMPLATE_CERT_SHA256   expected public certificate fingerprint
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
            signingConfig = if (localTestSigning) signingConfigs.getByName("debug") else envSigning
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
