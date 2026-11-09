function Get-QuestAdb {
    $candidates=@()
    if($env:ANDROID_SDK_ROOT){$candidates+=Join-Path $env:ANDROID_SDK_ROOT 'platform-tools\adb.exe'}
    if($env:ANDROID_HOME){$candidates+=Join-Path $env:ANDROID_HOME 'platform-tools\adb.exe'}
    $candidates+=Join-Path $env:LOCALAPPDATA 'Android\platform-tools\adb.exe'
    foreach($candidate in $candidates){if(Test-Path -LiteralPath $candidate){return $candidate}}
    $command=Get-Command adb -ErrorAction SilentlyContinue
    if($command){return $command.Source}
    throw 'Install Android platform-tools and add adb to PATH.'
}
function Resolve-QuestDevice([string]$Device='') {
    $adb=Get-QuestAdb
    if($Device){
        $model=((& $adb -s $Device shell getprop ro.product.model 2>$null)-join '').Trim()
        if($LASTEXITCODE -ne 0 -or $model -notmatch 'Quest'){throw "No authorized Quest at $Device"}
        return $Device
    }
    $found=@()
    foreach($line in (& $adb devices)){
        if($line -notmatch '^(\S+)\s+device\s*$'){continue};$id=$Matches[1]
        $model=((& $adb -s $id shell getprop ro.product.model 2>$null)-join '').Trim()
        if($model -notmatch 'Quest'){continue}
        $serial=((& $adb -s $id shell getprop ro.serialno)-join '').Trim()
        $found+=[pscustomobject]@{Id=$id;Serial=$serial;Usb=($id -eq $serial)}
    }
    if(!$found.Count){throw 'No authorized Quest connected. Plug in USB and allow debugging.'}
    if(@($found.Serial|Select-Object -Unique).Count -ne 1){throw 'Multiple Quests connected. Specify -Device.'}
    return ($found|Sort-Object Usb -Descending|Select-Object -First 1).Id
}
