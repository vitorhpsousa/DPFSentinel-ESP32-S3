// Standalone build: the pure-Kotlin core (decoding, regen/health logic, CSV) builds and tests on any
// JVM without the Android SDK:  ./gradlew -p core test   (from android/)
// The app pulls it in with includeBuild("core").
pluginManagement {
    repositories {
        gradlePluginPortal()
        mavenCentral()
    }
}
dependencyResolutionManagement {
    repositories {
        mavenCentral()
    }
}
rootProject.name = "core"
