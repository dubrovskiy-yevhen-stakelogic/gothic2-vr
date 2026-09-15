#requires -Version 5.1
[CmdletBinding()]
param([string]$Python, [string]$SdkRoot, [string]$JavaRoot, [switch]$VerifyOnly, [switch]$PlayerOnly)
. (Join-Path $PSScriptRoot 'release-common.ps1')
if ($VerifyOnly) { $env:GOTHIC2VR_OFFLINE='1' }
$Python = Resolve-Python $Python
$SdkRoot = Resolve-AndroidSdk $SdkRoot
if ($PlayerOnly) { Write-Host 'Player tools ready: Python and ADB.'; return }
$JavaRoot = Resolve-JavaRoot $JavaRoot
Initialize-AndroidBuildTools $SdkRoot $JavaRoot
$arguments = @('-B',(Join-Path $PSScriptRoot 'bootstrap-dependencies.py'))
if ($VerifyOnly) { $arguments += '--verify-only' }
& $Python @arguments
if ($LASTEXITCODE -ne 0) { throw 'Dependency preparation failed.' }
