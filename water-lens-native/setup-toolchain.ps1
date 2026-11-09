param([string]$ToolchainRoot=(Join-Path $env:LOCALAPPDATA 'Android\water-lens-toolchain'))
$ErrorActionPreference='Stop'
# Shared, pinned dependencies. No project-local SDK copies or signing keys.
$components=@(
    @{Name='ndk';Marker='ndk\android-ndk-r27c\source.properties';Url='https://dl.google.com/android/repository/android-ndk-r27c-windows.zip';Hash='27E49F11E0CEE5800983D8AF8F4ACD5BF09987AA6F790D4439DDA9F3643D2494'},
    @{Name='jdk';Marker='jdk\jdk-17.0.21+9\bin\java.exe';Url='https://api.adoptium.net/v3/binary/version/jdk-17.0.21%2B9/windows/x64/jdk/hotspot/normal/eclipse';Hash='98518ECF554465057EE5509462631DEEEE374BC368C12A411D225CD29381F84B'},
    @{Name='build-tools';Marker='build-tools\android-15\aapt2.exe';Url='https://dl.google.com/android/repository/build-tools_r35_windows.zip';Hash='5753C679A1B90BCF6FBC9945A2CE39DFB9E74F1DF0831A1E1866AE5B594326F0'},
    @{Name='platform';Marker='platform\android-35\android.jar';Url='https://dl.google.com/android/repository/platform-35_r02.zip';Hash='0988CACAD01B38A18A47BAC14A0695F246BC76C1B06C0EEB8EB0DC825AB0C8E0'},
    @{Name='openxr';Marker='openxr\prefab\modules\headers\include\openxr\openxr.h';Url='https://repo.maven.apache.org/maven2/org/khronos/openxr/openxr_loader_for_android/1.1.65/openxr_loader_for_android-1.1.65.aar';Hash='A298C326085B09181C85235FB04D37510939BD30938ED4322C093C09780E8643'}
)
New-Item -ItemType Directory -Path $ToolchainRoot -Force|Out-Null
foreach($component in $components){
    if(Test-Path -LiteralPath (Join-Path $ToolchainRoot $component.Marker)){Write-Output "Reusing $($component.Name)";continue}
    $archive=Join-Path $ToolchainRoot ($component.Name+'.zip')
    if(!(Test-Path -LiteralPath $archive)){
        Write-Output "Downloading $($component.Name)"
        Invoke-WebRequest -Uri $component.Url -OutFile $archive
    }
    if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $component.Hash){throw "Checksum mismatch: $archive"}
    Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $ToolchainRoot $component.Name) -Force
    if(!(Test-Path -LiteralPath (Join-Path $ToolchainRoot $component.Marker))){throw "Unexpected archive layout: $($component.Name)"}
}
Write-Output "Toolchain ready: $ToolchainRoot"
