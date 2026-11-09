param(
    [string]$Device='',
    [ValidateSet('Camera','Normal','Surface','SurfaceProbe')][string]$Mode='Camera',
    [ValidateRange(3,30)][int]$Seconds=8,
    [ValidateRange(0,1)][float]$Strength=.7,
    [ValidateRange(0,12)][float]$Speed=2,
    [ValidateRange(.3,2.5)][float]$Ripples=1.4,
    [ValidateRange(0,1)][float]$CrossWaves=.65,
    [ValidateRange(0,1)][float]$Sharpness=.6,
    [ValidateRange(1,3)][float]$ViewSize=1
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'quest-device.ps1')
$adb=Get-QuestAdb
$Device=Resolve-QuestDevice $Device
$serial=((& $adb -s $Device shell getprop ro.serialno)-join '').Trim()
$previousPath=$env:PATH
$env:PATH=(Split-Path $adb)+';'+$previousPath
$remote='/sdcard/Android/data/com.max.waterlens/files'
$stem=Join-Path $PSScriptRoot ('build\unattended-'+$Mode.ToLowerInvariant())
$statePath=Join-Path $PSScriptRoot 'build\bench-device-state.json'
New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot 'build') -Force | Out-Null
$result=[ordered]@{mode=$Mode;passed=$false;scope='Runtime and capture readiness; visual correctness still requires inspection';cleanup_restored=$false}
$changedProximity=$false;$changedBoundary=$false;$started=$false;$testError=$null
function Check-Exit([string]$step){if($LASTEXITCODE -ne 0){throw "$step failed (exit $LASTEXITCODE)"}}
function Read-Status {
    $raw=(& $adb -s $Device shell cat "$remote/bench-status.json" 2>$null)-join "`n"
    if($LASTEXITCODE -ne 0){return $null}
    try{return $raw|ConvertFrom-Json}catch{return $null}
}
function Test-Ready($status){
    if(!$status -or $status.pid -ne $script:questProcessId -or !$status.focused -or !$status.should_render -or !$status.head_valid -or !$status.views_valid){return $false}
    if($Mode -eq 'Camera'){return $status.camera_fresh -and $status.water_submitted}
    if($Mode -eq 'Normal'){return !$status.water_submitted -and !$status.surface_submitted}
    return $status.surface_submitted
}
try {
    # Recover this script's prior settings if its previous host process was killed.
    if(Test-Path -LiteralPath $statePath){
        $stale=Get-Content -Raw -LiteralPath $statePath|ConvertFrom-Json
        if($stale.pending){
            if($stale.serial -ne $serial){throw 'An interrupted test needs cleanup on a different Quest before starting another.'}
            if($stale.boundary_enabled){metavr --device $Device device boundary enable;Check-Exit 'Interrupted boundary cleanup'}
            if($stale.proximity_enabled){metavr --device $Device device proximity enable;Check-Exit 'Interrupted proximity cleanup'}
        }
    }
    $proximity=metavr --device $Device --json device proximity status|ConvertFrom-Json
    Check-Exit 'Read proximity state'
    $boundary=metavr --device $Device --json device boundary status|ConvertFrom-Json
    Check-Exit 'Read boundary state'
    if($null -eq $proximity.enabled -or $null -eq $boundary.enabled){throw 'Could not read the original device settings.'}
    $saved=[ordered]@{serial=$serial;pending=$true;proximity_enabled=$proximity.enabled;boundary_enabled=$boundary.enabled}
    $saved|ConvertTo-Json|Set-Content -LiteralPath $statePath -Encoding utf8
    # Pause the boundary only inside this stationary bench test. Do not wear or
    # walk with the headset during the run. Restore the exact original settings.
    if($proximity.enabled){
        $changedProximity=$true
        metavr --device $Device device proximity disable --duration-ms (120000+$Seconds*1000)
        Check-Exit 'Temporary proximity override'
    }
    if($boundary.enabled){$changedBoundary=$true;metavr --device $Device device boundary disable;Check-Exit 'Temporary boundary pause'}
    & $adb -s $Device shell rm -f "$remote/bench-status.json" "$remote/surface-test.txt"
    Check-Exit 'Clear prior test flags'
    $modeNumber=if($Mode -eq 'Camera'){0}elseif($Mode -eq 'Normal'){2}else{1}
    $values=@($modeNumber,$Strength,$Speed,$Ripples,$CrossWaves,$Sharpness,$ViewSize)|ForEach-Object {$_.ToString('G',[Globalization.CultureInfo]::InvariantCulture)}
    $config=Join-Path $PSScriptRoot 'build\bench-test.txt'
    Set-Content -LiteralPath $config -Value ($values -join ' ') -Encoding ascii
    & $adb -s $Device push $config "$remote/bench-test.txt";Check-Exit 'Push deterministic controls'
    if($Mode -eq 'SurfaceProbe'){
        $flag=Join-Path $PSScriptRoot 'build\surface-test.txt'
        Set-Content -LiteralPath $flag -Value '1' -Encoding ascii
        & $adb -s $Device push $flag "$remote/surface-test.txt";Check-Exit 'Enable surface probe'
    }
    & $adb -s $Device shell am force-stop com.max.waterlens
    $started=$true
    & $adb -s $Device shell am start -W -n com.max.waterlens/.WaterLensActivity;Check-Exit 'Launch app'
    $script:questProcessId=[int](((& $adb -s $Device shell pidof com.max.waterlens)-join '').Trim())
    $timer=[Diagnostics.Stopwatch]::StartNew();$previousFrames=-1;$stable=0;$ready=$null
    while($timer.Elapsed.TotalSeconds -lt 30){
        Start-Sleep -Milliseconds 750
        $status=Read-Status
        if((Test-Ready $status) -and $status.frames -gt $previousFrames){$stable++}else{$stable=0}
        if($status){$previousFrames=$status.frames}
        if($stable -ge 3 -and $timer.Elapsed.TotalSeconds -ge 6){$ready=$status;break}
    }
    if(!$ready){throw 'App never reached stable tracked rendering. Diagnostic logs and a screenshot will be saved.'}
    $result.start=$ready
    & $adb -s $Device shell screenrecord --time-limit $Seconds --size 1920x1080 --bit-rate 12000000 /sdcard/Download/water-lens-bench.mp4
    Check-Exit 'Record stereo compositor'
    $result.end=Read-Status
    if(!(Test-Ready $result.end) -or $result.end.frames -le $ready.frames){throw 'Rendering stopped during the capture.'}
    & $adb -s $Device pull /sdcard/Download/water-lens-bench.mp4 ($stem+'.mp4');Check-Exit 'Retrieve video'
    $result.passed=$true
} catch {
    $testError=$_;$result.error=$_.Exception.Message
} finally {
    try {
        & $adb -s $Device logcat -d -s WaterLens:V AndroidRuntime:E | Set-Content -LiteralPath ($stem+'.log') -Encoding utf8
        metavr --device $Device capture screenshot --method screencap --width 1024 --height 1024 --output ($stem+'.png')
    } catch {Write-Warning 'Could not collect all final diagnostics.'}
    $cleanupErrors=@()
    try {& $adb -s $Device shell rm -f "$remote/bench-test.txt" "$remote/surface-test.txt";Check-Exit 'Remove test flags'}catch{$cleanupErrors+=$_.Exception.Message}
    if($started){try{& $adb -s $Device shell am force-stop com.max.waterlens;Check-Exit 'Stop test app'}catch{$cleanupErrors+=$_.Exception.Message}}
    # Each restoration is attempted even if another device operation failed.
    if($changedBoundary){try{metavr --device $Device device boundary enable;Check-Exit 'Restore boundary'}catch{$cleanupErrors+=$_.Exception.Message}}
    if($changedProximity){try{metavr --device $Device device proximity enable;Check-Exit 'Restore proximity'}catch{$cleanupErrors+=$_.Exception.Message}}
    if(!$cleanupErrors.Count){
        if($saved){$saved.pending=$false;$saved|ConvertTo-Json|Set-Content -LiteralPath $statePath -Encoding utf8}
        $result.cleanup_restored=$true
    }else{$result.cleanup_error=$cleanupErrors -join '; ';Write-Warning $result.cleanup_error}
    $result|ConvertTo-Json -Depth 6|Set-Content -LiteralPath ($stem+'.json') -Encoding utf8
    $env:PATH=$previousPath
}
if($testError){throw $testError}
if(!$result.cleanup_restored){throw 'Test finished but device-setting restoration needs attention; see the JSON report.'}
Write-Output "PASS $Mode. Evidence: $stem.json, .mp4, .png, .log"
