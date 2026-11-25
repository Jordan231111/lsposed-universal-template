plugins {
    alias(libs.plugins.android.application) apply false
}

apply(from = "gradle/shared-signing.gradle")
