#requires -Version 5.1
[CmdletBinding()]
param([string]$SdkRoot, [string]$Serial)
. (Join-Path $PSScriptRoot 'quest-common.ps1')
$device=Select-Quest $SdkRoot $Serial
$result=Invoke-QuestAdb @('-s',$device.serial,'shell','am','start','-W','-a','com.gothic2vr.quest.IMPORT_GAME_FILES','-n','com.gothic2vr.quest/org.opengothic.app.SetupActivity')
if ($result.output -notmatch 'Status: ok') { throw "Could not open the import screen: $($result.output)" }
Write-Host 'On Quest, choose the game archive in Download/Gothic2VR and wait for import to finish.'
