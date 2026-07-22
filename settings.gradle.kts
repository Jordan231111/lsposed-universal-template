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
        // Classic Xposed API (de.robv.android.xposed:api) — required for the LSPatch (non-root) entry.
        maven { url = uri("https://api.xposed.info/") }
    }
}

rootProject.name = "LSPosedUniversalTemplate"
include(":app")
include(":modules:rogue")
include(":modules:once")
