#!/bin/bash

# --- KONFIGURASI PENCARIAN NDK ---
POSSIBLE_PATHS=(
    "$ANDROID_NDK_HOME"
    "$NDK_HOME"
    "$HOME/Android/Sdk/ndk"
    "/usr/local/lib/android/sdk/ndk"
    "/opt/android-sdk/ndk"
    "/usr/lib/android-sdk/ndk"
)

NDK_PATH=""

for base in "${POSSIBLE_PATHS[@]}"; do
    if [ -z "$base" ]; then continue; fi

    # 1. Cek apakah path ini langsung berisi ndk-build
    if [ -f "$base/build/ndk-build" ]; then
        NDK_PATH="$base"
        break
    fi

    # 2. Cek subfolder (biasanya folder versi seperti 27.x.x)
    if [ -d "$base" ]; then
        for sub in $(ls -d "$base"/*/ 2>/dev/null | sort -Vr); do
            if [ -f "${sub}build/ndk-build" ]; then
                NDK_PATH="${sub%/}"
                break 2
            fi
        done
    fi
done

if [ -z "$NDK_PATH" ]; then
    echo -e "\e[31mError: NDK tidak ditemukan!\e[0m"
    echo -e "\e[33mSilakan set environment variable ANDROID_NDK_HOME atau instal NDK di lokasi standar.\e[0m"
    exit 1
fi

echo -e "\e[36mUsing NDK at: $NDK_PATH\e[0m"

BUILD_SCRIPT="$NDK_PATH/build/ndk-build"

echo -e "\e[33m[BUILD] Starting NDK build process...\e[0m"

$BUILD_SCRIPT \
    NDK_PROJECT_PATH=. \
    APP_BUILD_SCRIPT=./Android.mk \
    NDK_APPLICATION_MK=./Application.mk \
    NDK_DEBUG=0

EXIT_CODE=$?

if [ $EXIT_CODE -eq 0 ]; then
    echo -e "\e[32m[BUILD] Build successful!\e[0m"
else
    echo -e "\e[31m[BUILD] Build failed with exit code $EXIT_CODE\e[0m"
fi

exit $EXIT_CODE
