#requires -Version 5.1
param([string]$SdkRoot, [string]$Python)
. (Join-Path $PSScriptRoot 'release-common.ps1')
$Python=Resolve-Python $Python
$argsList=@('-B',(Join-Path $PSScriptRoot 'verify-android.py'))
if ($SdkRoot) { $argsList += @('--sdk-root',$SdkRoot) }
& $Python @argsList
if ($LASTEXITCODE -ne 0) { throw 'APK verification failed.' }
