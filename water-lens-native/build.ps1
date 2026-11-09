param([string]$ToolchainRoot=(Join-Path $env:LOCALAPPDATA 'Android\water-lens-toolchain'))
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$toolchain = $ToolchainRoot
$ndk = Join-Path $toolchain 'ndk\android-ndk-r27c'
$java = Join-Path $toolchain 'jdk\jdk-17.0.21+9'
$android = Join-Path $toolchain 'platform\android-35\android.jar'
$bt = Join-Path $toolchain 'build-tools\android-15'
$xr = Join-Path $toolchain 'openxr\prefab\modules'
$clang = Join-Path $ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin'
$env:JAVA_HOME = $java
$env:PATH = "$java\bin;$env:PATH"
$out = Join-Path $root 'build'
New-Item -ItemType Directory -Path "$out\lib\arm64-v8a","$out\classes","$out\dex" -Force | Out-Null
function CheckExit([string]$step) { if ($LASTEXITCODE -ne 0) { throw "$step failed with exit code $LASTEXITCODE" } }
& "$clang\aarch64-linux-android29-clang.cmd" -O2 -fPIC -c "$ndk\sources\android\native_app_glue\android_native_app_glue.c" -o "$out\glue.o"
CheckExit 'Native activity glue'
$compileArgs = @('-std=c++17','-O3','-fPIC','-shared','-static-libstdc++','-Wall','-Wextra','-Wno-missing-field-initializers',
    '-DXR_USE_PLATFORM_ANDROID','-DXR_USE_GRAPHICS_API_OPENGL_ES','-DXR_USE_TIMESPEC',
    "-I$xr\headers\include","-I$ndk\sources\android\native_app_glue","-I$root\vendor",
    "$root\src\main.cpp","$out\glue.o","$xr\openxr_loader\libs\android.arm64-v8a\libopenxr_loader.so",
    '-landroid','-llog','-lEGL','-lGLESv3','-lcamera2ndk','-lmediandk',
    '-Wl,-u,ANativeActivity_onCreate','-Wl,-z,max-page-size=16384','-o',"$out\lib\arm64-v8a\libwaterlens.so")
& "$clang\aarch64-linux-android29-clang++.cmd" @compileArgs
CheckExit 'Stereo renderer'
Copy-Item -LiteralPath "$xr\openxr_loader\libs\android.arm64-v8a\libopenxr_loader.so" -Destination "$out\lib\arm64-v8a\libopenxr_loader.so" -Force
& "$java\bin\javac.exe" -source 8 -target 8 -bootclasspath $android -d "$out\classes" "$root\src\WaterLensActivity.java"
CheckExit 'Permission activity'
$classFiles = @(Get-ChildItem -LiteralPath "$out\classes" -Recurse -Filter '*.class' | ForEach-Object FullName)
& "$bt\d8.bat" --lib $android --min-api 29 --output "$out\dex" @classFiles
CheckExit 'Dex'
& "$bt\aapt2.exe" compile --dir "$root\res" -o "$out\resources.zip"
CheckExit 'Icon resources'
& "$bt\aapt2.exe" link -I $android --manifest "$root\AndroidManifest.xml" -o "$out\unsigned.apk" "$out\resources.zip"
CheckExit 'Android packaging'
& "$java\bin\jar.exe" uf "$out\unsigned.apk" -C $out lib -C "$out\dex" classes.dex -C $root assets
CheckExit 'Packaging libraries'
& "$bt\zipalign.exe" -f -P 16 4 "$out\unsigned.apk" "$out\aligned.apk"
CheckExit 'APK alignment'
$key = Join-Path $toolchain 'water-lens-debug.keystore'
if (!(Test-Path -LiteralPath $key)) {
    & "$java\bin\keytool.exe" -genkeypair -keystore $key -storepass android -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 -dname 'CN=Water Lens Development'
    CheckExit 'Development signing key'
}
& "$bt\apksigner.bat" sign --ks $key --ks-pass pass:android --key-pass pass:android --out "$root\water-lens-stereo.apk" "$out\aligned.apk"
CheckExit 'Signing'
& "$bt\apksigner.bat" verify "$root\water-lens-stereo.apk"
CheckExit 'Signature verification'
Write-Output "Built $root\water-lens-stereo.apk"
