#!/bin/bash
set -e

# --- 1. 环境变量配置 ---
ACS_FILES="/data/user/0/com.tom.rv2ide/files"
export PATH="$ACS_FILES/usr/bin:$PATH"
export JAVA_HOME="$ACS_FILES/usr/lib/jvm/java-21-openjdk"
export ANDROID_HOME="$ACS_FILES/home/android-sdk"
export ANDROID_NDK_HOME="${ANDROID_HOME}/ndk/29.0.14033849"
export TMPDIR="$ACS_FILES/tmp"
mkdir -p "$TMPDIR"

APP_COMPONENT="com.zero.xiangqi/.MainActivity"
PROJECT_PATH="$PWD"
rm -rf "$PROJECT_PATH/app/build/outputs/apk"

# --- 2. 执行构建 ---
echo "=> 开始执行 Gradle 构建..."
set +e
sh ./gradlew assembleDebug --console=plain
BUILD_STATUS=$?
set -e

if [ $BUILD_STATUS -ne 0 ]; then
    echo "=> 错误: Gradle 构建失败。" >&2
    exit 1
fi

# --- 3. 安装并自动拉起 logcat 监控 ---
HOST_APK_PATH=$(find "$PROJECT_PATH/app/build/outputs/apk" -name "*.apk" | head -n 1)

if [ -n "$HOST_APK_PATH" ] && [ -f "$HOST_APK_PATH" ]; then
    TARGET_TMP_APK="/data/local/tmp/app_build_$(date +%s).apk"
    cp "$HOST_APK_PATH" "$TARGET_TMP_APK"
    chmod 644 "$TARGET_TMP_APK"
    
    echo "=> 正在安装 APK..."
    if INSTALL_OUT=$(pm install -r "$TARGET_TMP_APK" 2>&1); then
        /system/bin/am start -n "$APP_COMPONENT" > /dev/null 2>&1
        echo "=> 应用已启动！"
        rm -f "$TARGET_TMP_APK"
        rm -f "$HOST_APK_PATH"
        
        # 【核心修复】自动清空旧日志并实时打印 XiangQiTouch, XiangQiAI, XiangQiJNI 标签

    else
        echo "=> 错误: APK 安装失败: $INSTALL_OUT" >&2
        rm -f "$TARGET_TMP_APK"
        exit 1
    fi
else
    echo "=> 错误: 未生成目标 APK。" >&2
    exit 1
fi
