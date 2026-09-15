#requires -Version 5.1
[CmdletBinding()]
param([string]$GameDir, [string]$Serial, [string]$SdkRoot, [switch]$UpdateOnly, [switch]$DryRun)
. (Join-Path $PSScriptRoot 'quest-common.ps1')
$root=$script:ReleaseRoot
$SdkRoot=Resolve-AndroidSdk $SdkRoot
$device=Select-Quest $SdkRoot $Serial -NoDriverInstall:$DryRun
$Serial=$device.serial
if ($UpdateOnly -or $DryRun) {
  & (Join-Path $PSScriptRoot 'install-quest.ps1') -SdkRoot $SdkRoot -Serial $Serial -DryRun:$DryRun
  return
}
$existing=Invoke-QuestAdb @('-s',$Serial,'shell','test','-d','/sdcard/Android/data/com.gothic2vr.quest/files/Gothic2/Data') -AllowFailure
if ($existing.code -eq 0 -and !$GameDir) {
  & (Join-Path $PSScriptRoot 'install-quest.ps1') -SdkRoot $SdkRoot -Serial $Serial
  return
}
$Python=Resolve-Python ''
$manifest=Join-Path $root 'build/private/game-data.manifest.json'
if ((Test-Path -LiteralPath $manifest) -and !$GameDir) {
  Write-Host 'Verifying the previously prepared game archive.'
  & $Python -B (Join-Path $PSScriptRoot 'verify-game-data.py')
  if ($LASTEXITCODE -ne 0) { throw 'The previous game archive failed verification.' }
} else {
  if (!$GameDir) {
    $GameDir=Read-Host 'Path to your installed Gothic II Gold / Night of the Raven folder (contains Data and System)'
    $GameDir=$GameDir.Trim().Trim('"')
  }
  if (!$GameDir -or !(Test-Path -LiteralPath $GameDir -PathType Container)) { throw 'A valid purchased game installation folder is required.' }
  & $Python -B (Join-Path $PSScriptRoot 'package-game-data.py') --game-root $GameDir
  if ($LASTEXITCODE -ne 0) { throw 'Game data packaging failed. No game files were transferred.' }
}
& (Join-Path $PSScriptRoot 'install-quest.ps1') -SdkRoot $SdkRoot -Serial $Serial -WithGameData
& (Join-Path $PSScriptRoot 'open-data-import.ps1') -SdkRoot $SdkRoot -Serial $Serial
Write-Host 'In the headset, select the ZIP under Download/Gothic2VR and wait for import to finish.'
