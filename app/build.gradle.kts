@file:Suppress("UnstableApiUsage")

plugins {
    alias(libs.plugins.android.application)
}

android {
    namespace = "com.zero.xiangqi"
    //noinspection GradleDependency
    compileSdk = 37
    ndkVersion = "29.0.14033849"
    
    defaultConfig {
        applicationId = "com.zero.xiangqi"
        minSdk = 26
        //noinspection OldTargetApi
        targetSdk = 36

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++17", "-O3", "-fno-rtti", "-fno-exceptions")
                abiFilters += listOf("arm64-v8a")
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }

    // ==========================================
    // 签名配置：将 Debug 和 Release 的密钥与别名规范隔离
    // ==========================================
    signingConfigs {
        create("debugConfig") {
            storeFile = file("${rootProject.projectDir}/app/debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
        create("releaseConfig") {
            storeFile = file("${rootProject.projectDir}/app/debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
    }

    buildTypes {
        getByName("debug") {
            signingConfig = signingConfigs.getByName("debugConfig")
        }
        getByName("release") {
            signingConfig = signingConfigs.getByName("releaseConfig")
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }

    packaging {
        resources.excludes.add("META-INF/**")
        // 告诉编译器：这是一个伪装成 so 的神经网络权重文件，不要去 Strip 它
        jniLibs.keepDebugSymbols.add("**/libpikafishnnue.so")
        
        // [核心修复] 强制让系统把 so 解压到物理目录，这样 execl 就能真正找到了
        jniLibs.useLegacyPackaging = true
    }
}

// kotlin 配置块必须放在 android {} 外部
kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_1_8)
    }
}

dependencies {
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)
    implementation(libs.androidx.core.ktx)
    implementation(libs.material)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(libs.androidx.junit)
}