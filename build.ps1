# --- KONFIGURASI PENCARIAN NDK ---
$PossibleNDKPaths = @(
    $env:ANDROID_NDK_HOME,
    $env:NDK_HOME,
    "$env:LOCALAPPDATA/Android/Sdk/ndk",
    "C:/Android/android-sdk/ndk",
    "C:/Microsoft/AndroidSDK/ndk",
    "C:/Program Files (x86)/Android/android-sdk/ndk"
)

$NDKPath = $null

foreach ($BaseDir in $PossibleNDKPaths) {
    if (-not $BaseDir) { continue }

    # 1. Cek apakah path ini langsung berisi ndk-build
    if (Test-Path "$BaseDir/build/ndk-build") {
        $NDKPath = $BaseDir
        break
    }

    # 2. Jika tidak, cek subfolder di dalamnya (untuk lokasi standar SDK)
    if (Test-Path $BaseDir) {
        $SubFolders = Get-ChildItem -Path $BaseDir -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending
        foreach ($Folder in $SubFolders) {
            if (Test-Path "$($Folder.FullName)/build/ndk-build") {
                $NDKPath = $Folder.FullName
                break 2
            }
        }
    }
}

if (-not $NDKPath) {
    Write-Host "Error: NDK tidak ditemukan!" -ForegroundColor Red
    Write-Host "Silakan set environment variable ANDROID_NDK_HOME atau instal NDK di lokasi standar." -ForegroundColor Yellow
    exit 1
}

Write-Host "Using NDK at: $NDKPath" -ForegroundColor Cyan

# Menentukan script build berdasarkan OS
$buildScript = "$NDKPath/build/ndk-build"
if ($IsWindows -or $env:OS -eq "Windows_NT") {
    $buildScript += ".cmd"
}

if (-not (Test-Path $buildScript)) {
    Write-Host "Error: NDK build script tidak ditemukan di $buildScript" -ForegroundColor Red
    exit 1
}

Write-Host "[BUILD] Starting NDK build process..." -ForegroundColor Yellow

& $buildScript `
    NDK_PROJECT_PATH="$PSScriptRoot" `
    APP_BUILD_SCRIPT="$PSScriptRoot/Android.mk" `
    NDK_APPLICATION_MK="$PSScriptRoot/Application.mk" `
    NDK_DEBUG=0

if ($LASTEXITCODE -eq 0) {
    Write-Host "[BUILD] Build successful!" -ForegroundColor Green
} else {
    Write-Host "[BUILD] Build failed with exit code $LASTEXITCODE" -ForegroundColor Red
}

exit $LASTEXITCODE
