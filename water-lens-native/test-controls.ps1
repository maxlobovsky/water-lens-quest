param(
    [string]$Device = '',
    [switch]$Preview
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'quest-device.ps1')
$adb = Get-QuestAdb
$Device = Resolve-QuestDevice $Device
$clang = Join-Path $env:LOCALAPPDATA 'Android\water-lens-toolchain\ndk\android-ndk-r27c\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android29-clang++.cmd'
Push-Location $PSScriptRoot
try {
    & $clang -std=c++17 -O2 -static-libstdc++ tests\panel_controls_test.cpp -o build\panel-controls-test
    if ($LASTEXITCODE -ne 0) { throw 'Panel test compilation failed.' }
    & $adb -s $Device push build\panel-controls-test /data/local/tmp/water-lens-panel-test
    if ($LASTEXITCODE -ne 0) { throw 'Test deployment failed.' }
    & $adb -s $Device shell chmod 700 /data/local/tmp/water-lens-panel-test
    if ($LASTEXITCODE -ne 0) { throw 'Test setup failed.' }
    & $adb -s $Device shell /data/local/tmp/water-lens-panel-test
    if ($LASTEXITCODE -ne 0) { throw 'Panel controls tests failed.' }
    if ($Preview) {
        & $adb -s $Device exec-out /data/local/tmp/water-lens-panel-test --preview | Set-Content -LiteralPath build\panel-preview.json -Encoding utf8
        if ($LASTEXITCODE -ne 0) { throw 'Panel preview export failed.' }
        & $adb -s $Device exec-out /data/local/tmp/water-lens-panel-test --preview-closed | Set-Content -LiteralPath build\reopen-preview.json -Encoding utf8
        if ($LASTEXITCODE -ne 0) { throw 'Reopening preview export failed.' }
    }
} finally { Pop-Location }
