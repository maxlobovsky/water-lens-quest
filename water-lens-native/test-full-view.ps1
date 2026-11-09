param(
    [string]$Device = '',
    [switch]$SurfaceProbe
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'quest-device.ps1')
$adb = Get-QuestAdb
$adbDirectory = Split-Path $adb
$Device = Resolve-QuestDevice $Device
$previousPath = $env:PATH
$capturePath = Join-Path $PSScriptRoot $(if ($SurfaceProbe) { 'surface-probe.mp4' } else { 'build\headset-check.mp4' })
$remoteCapture = '/sdcard/Download/water-lens-diagnostic.mp4'
$logName = if ($SurfaceProbe) { 'build\surface-probe.log' } else { 'build\camera-current.log' }
$remoteFlag = '/sdcard/Android/data/com.max.waterlens/files/surface-test.txt'
$flagPushed = $false
try {
    $env:PATH = "$adbDirectory;$previousPath"
    $power = (& $adb -s $Device shell dumpsys power) -join "`n"
    if ($power -notmatch 'mWakefulness=Awake') { throw 'Put on the Quest and clear tracking before this test.' }
    if ($SurfaceProbe) {
        $localFlag = Join-Path $PSScriptRoot 'build\surface-test.txt'
        Set-Content -LiteralPath $localFlag -Value '1' -Encoding ascii
        & $adb -s $Device push $localFlag $remoteFlag
        if ($LASTEXITCODE -ne 0) { throw 'Could not enable the one-launch surface visibility check.' }
        $flagPushed = $true
    }
    metavr --device $Device app launch com.max.waterlens --cold-start --verify --verify-window 3
    if ($LASTEXITCODE -ne 0) { throw 'App launch failed.' }
    # Horizon OS can reject MetaCam's background recording service. Android's
    # bounded screenrecord works on this Quest and captures both compositor eyes.
    & $adb -s $Device shell screenrecord --time-limit 8 --size 1920x1080 --bit-rate 12000000 $remoteCapture
    if ($LASTEXITCODE -ne 0) { throw 'Stereo screen recording failed.' }
    & $adb -s $Device pull $remoteCapture $capturePath
    if ($LASTEXITCODE -ne 0) { throw 'Stereo recording retrieval failed.' }
    metavr --device $Device capture screenshot --method screencap --output (Join-Path $PSScriptRoot 'build\full-view-stereo.png')
    if ($LASTEXITCODE -ne 0) { Write-Warning 'Stereo screenshot unavailable; video capture will still be retrieved.' }
} finally {
    & $adb -s $Device logcat -d -s WaterLens:V AndroidRuntime:E | Set-Content -LiteralPath (Join-Path $PSScriptRoot $logName) -Encoding utf8
    if ($flagPushed) {
        & $adb -s $Device shell rm -f $remoteFlag
        metavr --device $Device app launch com.max.waterlens --cold-start --verify --verify-window 3
        if ($LASTEXITCODE -ne 0) { Write-Warning 'Could not relaunch Camera mode; reopen Water Lens from the Quest library.' }
    }
    $env:PATH = $previousPath
}
Write-Output $capturePath
