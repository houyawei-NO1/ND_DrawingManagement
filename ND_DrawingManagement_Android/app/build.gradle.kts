import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.houyawei.nddrawing"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.houyawei.nddrawing"
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"
        // 只打 64 位，显著减小包体积
        ndk { abiFilters += listOf("arm64-v8a") }
        // 每次编译自动生成构建日期，关于页直接显示，无需再手工改版本号
        val buildDate = SimpleDateFormat("yyyy-MM-dd", Locale.US).format(Date())
        buildConfigField("String", "BUILD_DATE", "\"$buildDate\"")
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
        debug {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }

    packaging {
        jniLibs {
            // CameraX(camera-core) 自带的该原生库未做 16KB 页对齐，会在 Android 15 触发兼容性警告。
            // 本项目只用 YUV_420_888 分析（直接读 Y 平面解码二维码），未使用其 YUV→RGB 转换，可安全排除。
            excludes += "**/libimage_processing_util_jni.so"
        }
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }
    composeOptions { kotlinCompilerExtensionVersion = "1.5.8" }
}

dependencies {
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.7.0")
    implementation("androidx.activity:activity-compose:1.8.2")

    implementation(platform("androidx.compose:compose-bom:2024.02.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.navigation:navigation-compose:2.7.7")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")

    // 二维码扫描：CameraX 取景 + ZXing 纯 Java 解码（不引入任何原生 .so，规避 16KB 页对齐警告）
    implementation("androidx.camera:camera-camera2:1.3.4")
    implementation("androidx.camera:camera-lifecycle:1.3.4")
    implementation("androidx.camera:camera-view:1.3.4")
    implementation("com.google.zxing:core:3.5.3")

    debugImplementation("androidx.compose.ui:ui-tooling")
}
