pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "DPFSentinel-Android"

// Pure-Kotlin logic shared with the tests (builds without the Android SDK: ./gradlew -p core test).
includeBuild("core")
include(":app")
