#requires -Version 5.1
[CmdletBinding()]
param([string]$SdkRoot, [string]$Serial, [switch]$WithGameData, [switch]$DryRun)
. (Join-Path $PSScriptRoot 'quest-common.ps1')
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$lock=Get-Content -LiteralPath (Join-Path $root 'config/android-toolchain.lock.json') -Raw | ConvertFrom-Json
$releasePath=Join-Path $root 'release.json'
if (Test-Path -LiteralPath $releasePath) {
  $release=Get-Content -LiteralPath $releasePath -Raw | ConvertFrom-Json
  if ($release.apk.file -notmatch '^[A-Za-z0-9_.-]+\.apk$' -or $release.applicationId -ne $lock.applicationId) { throw 'Invalid release manifest.' }
  $apk=Join-Path $root $release.apk.file
  $apkHash=(Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash
  if ($apkHash -ne $release.apk.sha256) { throw 'APK checksum mismatch. Extract a fresh copy of the release archive.' }
} else {
  $build=Get-Content -LiteralPath (Join-Path $root 'build/android/build-manifest.json') -Raw | ConvertFrom-Json
  $verified=Get-Content -LiteralPath (Join-Path $root 'build/android/artifact-verification.json') -Raw | ConvertFrom-Json
  $apk=$build.apk.path
  $apkHash=(Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash
  if ($build.status -ne 'built' -or $verified.status -ne 'passed' -or $build.applicationId -ne $lock.applicationId -or
      $apkHash -ne $build.apk.sha256 -or $apkHash -ne $verified.sha256) { throw 'Current APK verification is required. Run VERIFY-APK.bat.' }
}
$archive=$null
$dataHash=$null
$remoteArchive=$null
if ($WithGameData) {
  $data=Get-Content -LiteralPath (Join-Path $root 'build/private/game-data.manifest.json') -Raw | ConvertFrom-Json
  $dataVerified=Get-Content -LiteralPath (Join-Path $root 'build/private/game-data.verification.json') -Raw | ConvertFrom-Json
  $archive=$data.output
  $dataHash=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
  if ($data.status -ne 'packaged' -or $dataVerified.status -ne 'passed' -or $dataHash -ne $data.sha256 -or $dataHash -ne $dataVerified.sha256) { throw 'Current private game-data verification is required.' }
  $remoteArchive='/sdcard/Download/Gothic2VR/game-data-'+$dataHash.Substring(0,12).ToLowerInvariant()+'.zip'
}
$device=Select-Quest $SdkRoot $Serial -NoDriverInstall:$DryRun
$Serial=$device.serial
$sdk=[int](Invoke-QuestAdb @('-s',$Serial,'shell','getprop','ro.build.version.sdk')).output
if ($sdk -lt $lock.minSdk) { throw 'Device Android API is below the APK minimum.' }
$package=Invoke-QuestAdb @('-s',$Serial,'shell','pm','path',$lock.applicationId) -AllowFailure
if ($package.code -notin @(0,1)) { throw 'Could not query existing application.' }
$before=[ordered]@{}
$files='/sdcard/Android/data/'+$lock.applicationId+'/files'
if ($package.output -match '^package:') {
  $listing=Invoke-QuestAdb @('-s',$Serial,'shell','ls','-1',$files) -AllowFailure
  if ($listing.code -eq 0) {
    $names=@($listing.output -split '\r?\n' | Where-Object { $_ -match '^[A-Za-z0-9_.-]+\.(sav|ini)$' } | Sort-Object)
    foreach ($name in $names) { $before[$name]=(Invoke-QuestAdb @('-s',$Serial,'shell','sha256sum',($files+'/'+$name))).output.Split(' ')[0] }
  } else {
    $filesExist=Invoke-QuestAdb @('-s',$Serial,'shell','test','-d',$files) -AllowFailure
    if ($filesExist.code -eq 0) { throw 'Existing save/settings directory cannot be read; update stopped before installation.' }
  }
}
$requiredBytes=536870912
if ($WithGameData) { $requiredBytes += [long]$data.bytes + [long]$data.archive_bytes }
$space=(Invoke-QuestAdb @('-s',$Serial,'shell','df','-k','/sdcard')).output -split '\r?\n' | Select-Object -Last 1
$columns=$space.Trim() -split '\s+'
$availableBytes=[long]$columns[3]*1024
if ($availableBytes -lt $requiredBytes) { throw 'Not enough free space for installation/import and a 512 MiB reserve.' }
$report=[ordered]@{
  schemaVersion=1;mode=$(if($DryRun){'dry-run'}else{'install'});device=$Serial;model=$device.model;
  applicationId=$lock.applicationId;apkSha256=$apkHash;gameDataSha256=$dataHash;archiveOnDevice=$remoteArchive;
  previousPackage=$package.output;installed=$false;dataTransferred=$false;launched=$false;headsetValidated=$false;
  preserved=$null;before=$before;after=[ordered]@{}
}
if ($DryRun) { $report | ConvertTo-Json -Depth 6; return }
$reports=Join-Path $root 'build/android/deployments'
$null=New-Item -ItemType Directory -Path $reports -Force
$reportPath=Join-Path $reports ([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')+'.json')
try {
  $result=Invoke-QuestAdb @('-s',$Serial,'install','--no-streaming','-r',$apk)
  if ($result.output -notmatch '(?m)^Success\s*$') { throw "Installer did not report Success: $($result.output)" }
  $report.installed=$true
  $remotePackage=(Invoke-QuestAdb @('-s',$Serial,'shell','pm','path',$lock.applicationId)).output
  if ($remotePackage -notmatch '^package:([^\r\n]+)$') { throw 'Expected one installed APK path.' }
  $remoteApk=$Matches[1]
  $installedHash=(Invoke-QuestAdb @('-s',$Serial,'shell','sha256sum',$remoteApk)).output.Split(' ')[0]
  if ($installedHash -ne $apkHash) { throw 'Installed APK hash differs from the verified build.' }
  $report['installedApkSha256']=$installedHash
  $report.preserved=$true
  foreach ($name in $before.Keys) {
    $report.after[$name]=(Invoke-QuestAdb @('-s',$Serial,'shell','sha256sum',($files+'/'+$name))).output.Split(' ')[0]
    if ($report.after[$name] -ne $before[$name]) { $report.preserved=$false }
  }
  if (!$report.preserved) { throw 'A save/settings hash changed during installation. Inspect the deployment report.' }
  if ($WithGameData) {
    $null=Invoke-QuestAdb @('-s',$Serial,'shell','mkdir','-p','/sdcard/Download/Gothic2VR')
    $exists=Invoke-QuestAdb @('-s',$Serial,'shell','test','-f',$remoteArchive) -AllowFailure
    if ($exists.code -eq 0) {
      $remoteHash=(Invoke-QuestAdb @('-s',$Serial,'shell','sha256sum',$remoteArchive)).output.Split(' ')[0]
      if ($remoteHash -ne $dataHash) { throw 'Existing destination differs; preserved without replacement.' }
    } else {
      $partial=$remoteArchive+'.part'
      $partialExists=Invoke-QuestAdb @('-s',$Serial,'shell','test','-e',$partial) -AllowFailure
      if ($partialExists.code -eq 0) { throw "A previous partial transfer is preserved: $partial. Inspect it before retrying." }
      $null=Invoke-QuestAdb @('-s',$Serial,'push',$archive,$partial)
      $remoteHash=(Invoke-QuestAdb @('-s',$Serial,'shell','sha256sum',$partial)).output.Split(' ')[0]
      if ($remoteHash -ne $dataHash) { throw 'Transferred archive hash mismatch; partial file retained.' }
      $null=Invoke-QuestAdb @('-s',$Serial,'shell','mv',$partial,$remoteArchive)
    }
    $report.dataTransferred=$true
    Write-Host 'Game ZIP is ready in Download/Gothic2VR. Run OPEN-DATA-IMPORT.bat when ready to open the importer on Quest.'
  }
  $report['status']='passed'
  Write-Host 'APK installed and verified. The game has not been launched.'
} catch {
  $report['status']='failed'
  $report['error']=$_.Exception.Message
  throw
} finally {
  $report['completedUtc']=[DateTime]::UtcNow.ToString('o')
  $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
}
