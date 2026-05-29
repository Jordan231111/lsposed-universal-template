pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode = RepositoriesMode.FAIL_ON_PROJECT_REPOS
    repositories {
        google()
        mavenCentral()
        // Classic Xposed API JAR for the lspatch build flavor (api 82). LSPatch loads modules via
        // the legacy IXposedHookLoadPackage entry point and that interface lives in this artifact.
        maven { url = uri("https://api.xposed.info/") }
    }
}

rootProject.name = "LSPosedUniversalTemplate"
include(":app")
