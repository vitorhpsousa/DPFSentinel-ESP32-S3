plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "io.github.vitorhpsousa.dpfsentinel"
    compileSdk = 35

    defaultConfig {
        applicationId = "io.github.vitorhpsousa.dpfsentinel"
        minSdk = 26
        targetSdk = 35
        versionCode = 3
        versionName = "1.2"
    }

    // One fixed key for every build, so a new APK installs as an update (keeping settings and logs)
    // instead of failing on a signature mismatch: each CI runner otherwise makes a fresh debug key.
    // CI writes it from the SIDELOAD_KEYSTORE_B64 secret; the key is never committed. Without it
    // (a local build) the usual debug key is used.
    val sideloadStore = System.getenv("SIDELOAD_KEYSTORE")?.let { file(it) }?.takeIf { it.exists() }
    signingConfigs {
        if (sideloadStore != null) {
            create("sideload") {
                storeFile = sideloadStore
                storePassword = System.getenv("SIDELOAD_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("SIDELOAD_KEY_ALIAS") ?: "dpfsentinel"
                keyPassword = System.getenv("SIDELOAD_KEYSTORE_PASSWORD")
            }
        }
    }
    val signing = signingConfigs.findByName("sideload") ?: signingConfigs.getByName("debug")

    buildTypes {
        debug {
            signingConfig = signing
        }
        release {
            isMinifyEnabled = false
            signingConfig = signing
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }
}

dependencies {
    implementation("io.github.vitorhpsousa.dpfsentinel:core")

    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")
    implementation(platform("androidx.compose:compose-bom:2024.10.01"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")

    // Android Auto (Car App Library templates)
    implementation("androidx.car.app:app:1.4.0")
}
