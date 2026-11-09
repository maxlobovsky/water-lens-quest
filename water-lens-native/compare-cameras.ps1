param([string]$Device = '')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'quest-device.ps1')
$adb = Get-QuestAdb
$Device = Resolve-QuestDevice $Device
$env:PATH = (Split-Path $adb) + ';' + $env:PATH
$remote = '/sdcard/Android/data/com.max.waterlens/files'
Push-Location $PSScriptRoot
try {
    # Zero requests two captures within one tracking space, two seconds apart.
    Set-Content -LiteralPath build\camera-test.txt -Value 0 -Encoding ascii
    & $adb -s $Device push build\camera-test.txt "$remote/camera-test.txt"
    if ($LASTEXITCODE -ne 0) { throw 'Diagnostic setup failed' }
    foreach ($size in 1280,1728) { foreach ($eye in 0,1) { foreach ($suffix in 'raw.ppm','render.ppm','alpha.pgm') {
        & $adb -s $Device shell rm -f "$remote/camera-$size-eye$eye-$suffix"
    } } }
    metavr --device $Device app launch com.max.waterlens --cold-start
    if ($LASTEXITCODE -ne 0) { throw 'App launch failed' }
    & $adb -s $Device shell am broadcast -a com.oculus.vrpowermanager.prox_close
    $captured = $false
    for ($attempt=0; $attempt -lt 12; $attempt++) {
        Start-Sleep -Seconds 2
        & $adb -s $Device shell test -s "$remote/camera-1728-eye1-alpha.pgm"
        if ($LASTEXITCODE -eq 0) { $captured = $true; break }
    }
    $appPid = ((& $adb -s $Device shell pidof com.max.waterlens) -join '').Trim()
    if ($appPid) {
        & $adb -s $Device logcat -d --pid $appPid -s WaterLens | Set-Content -LiteralPath build\camera-comparison.log
        Get-Content -LiteralPath build\camera-comparison.log | Select-String 'DIAG|STATS|CAMERA|error|failed'
    }
    if (!$captured) { throw 'Diagnostic incomplete; check headset tracking/focus' }
    foreach ($size in 1280,1728) { foreach ($eye in 0,1) { foreach ($suffix in 'raw.ppm','render.ppm','alpha.pgm') {
        & $adb -s $Device pull "$remote/camera-$size-eye$eye-$suffix" "build\camera-$size-eye$eye-$suffix"
        if ($LASTEXITCODE -ne 0) { throw 'Diagnostic download failed' }
    } } }
} finally {
    & $adb -s $Device shell rm -f "$remote/camera-test.txt"
    metavr --device $Device app launch com.max.waterlens --cold-start --verify --verify-window 3
    & $adb -s $Device shell am broadcast -a com.oculus.vrpowermanager.automation_disable
    Pop-Location
}
