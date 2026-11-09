param([string]$Device='',[string]$LastKnownAddress='')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'quest-device.ps1')
$adb=Get-QuestAdb
if($LastKnownAddress -and $LastKnownAddress -notmatch '^\d+\.\d+\.\d+\.\d+:\d+$'){throw 'Expected an IPv4 address and port.'}
try {$selected=Resolve-QuestDevice $Device} catch {
    if($Device){throw}
    $services=@()
    foreach($line in (& $adb mdns services)){
        if($line -match '^adb-\S+\s+_adb\._tcp\s+(\d+\.\d+\.\d+\.\d+:\d+)'){$services+=$Matches[1]}
    }
    if($LastKnownAddress){$endpoint=$LastKnownAddress}
    elseif($services.Count -eq 1){$endpoint=$services[0]}
    else {throw 'Connect USB once, or specify -LastKnownAddress. No unique wireless Quest was advertised.'}
    & $adb connect $endpoint | Write-Host
    $selected=Resolve-QuestDevice $endpoint
}
$serial=((& $adb -s $selected shell getprop ro.serialno)-join '').Trim()
if($selected -ne $serial){Write-Output $selected;exit 0}
$address=(& $adb -s $selected shell ip -4 addr show wlan0)-join ' '
if($address -notmatch '\binet\s+(\d+\.\d+\.\d+\.\d+)/'){throw 'The Quest is not connected to Wi-Fi.'}
$endpoint=$Matches[1]+':5555'
$port=((& $adb -s $selected shell getprop service.adb.tcp.port)-join '').Trim()
if($port -ne '5555'){
    & $adb -s $selected tcpip 5555 | Write-Host
    if($LASTEXITCODE -ne 0){throw 'Could not enable wireless ADB.'}
}
& $adb connect $endpoint | Write-Host
$verified=Resolve-QuestDevice $endpoint
$actualSerial=((& $adb -s $verified shell getprop ro.serialno)-join '').Trim()
if($actualSerial -ne $serial){throw 'Wireless endpoint does not match the USB Quest.'}
Write-Host 'Verified the same Quest over Wi-Fi. USB can be disconnected.'
Write-Output $verified
