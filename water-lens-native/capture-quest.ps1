param(
    [string]$Device = '',
    [string]$OutputPath = (Join-Path $PSScriptRoot 'build\headset-latest.png'),
    [switch]$WakeForCapture
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'quest-device.ps1')
$adb = Get-QuestAdb
$adbDirectory = Split-Path $adb
$Device = Resolve-QuestDevice $Device
if (!(Get-Command metavr -ErrorAction SilentlyContinue)) { throw 'Meta VR CLI is missing. Install the official metavr npm package globally.' }
$previousPath = $env:PATH
$changedWake = $false
try {
    $env:PATH = "$adbDirectory;$previousPath"
    if ($WakeForCapture) {
        $power = (& $adb -s $Device shell dumpsys power) -join "`n"
        if ($power -match 'mWakefulness=Asleep') {
            & $adb -s $Device shell am broadcast -a com.oculus.vrpowermanager.prox_close
            if ($LASTEXITCODE -ne 0) { throw 'Could not temporarily wake the headset.' }
            $changedWake = $true
        }
    }
    metavr --device $Device capture screenshot --method screencap --width 1536 --height 1536 --output $OutputPath
    if ($LASTEXITCODE -ne 0) { throw 'Quest compositor capture failed.' }
    $capture = Get-Item -LiteralPath $OutputPath -ErrorAction Stop
    if ($capture.Length -lt 8) { throw 'The compositor returned an empty capture. Let the app finish resuming, then capture again.' }
    $signature = [System.IO.File]::ReadAllBytes($capture.FullName)
    $png = $signature[0] -eq 137 -and $signature[1] -eq 80 -and $signature[2] -eq 78 -and $signature[3] -eq 71
    $jpeg = $signature[0] -eq 255 -and $signature[1] -eq 216
    if (!$png -and !$jpeg) { throw 'The capture has no supported PNG or JPEG signature.' }
    # metacam can return JPEG data even when its requested output suffix is .png.
    Write-Output $OutputPath
} finally {
    if ($changedWake) { & $adb -s $Device shell am broadcast -a com.oculus.vrpowermanager.automation_disable }
    $env:PATH = $previousPath
}
