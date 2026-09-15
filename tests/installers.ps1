#requires -Version 5.1
# Run the unchanged deployment script against a simulated ADB boundary.
param([string]$Output)
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path -Parent $PSScriptRoot
if (!$Output) { $Output=Join-Path $sourceRoot 'build/installer-tests' }
$fixture=Join-Path $Output ([Guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path (Join-Path $fixture 'tools'),(Join-Path $fixture 'config'),(Join-Path $fixture 'build/private') -Force
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tools/install-quest.ps1') -Destination (Join-Path $fixture 'tools/install-quest.ps1')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'config/android-toolchain.lock.json') -Destination (Join-Path $fixture 'config/android-toolchain.lock.json')
Set-Content -LiteralPath (Join-Path $fixture 'test.apk') -Value 'Synthetic APK checksum fixture. Not an application.'
Set-Content -LiteralPath (Join-Path $fixture 'build/private/game-data.zip') -Value 'Synthetic transfer fixture. No game data.'
$global:QuestTestApkHash=(Get-FileHash -LiteralPath (Join-Path $fixture 'test.apk')).Hash
$global:QuestTestDataHash=(Get-FileHash -LiteralPath (Join-Path $fixture 'build/private/game-data.zip')).Hash
@{applicationId='com.gothic2vr.quest';apk=@{file='test.apk';sha256=$global:QuestTestApkHash}} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'release.json')
@{status='packaged';output=(Join-Path $fixture 'build/private/game-data.zip');sha256=$global:QuestTestDataHash;bytes=100;archive_bytes=100} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'build/private/game-data.manifest.json')
@{status='passed';sha256=$global:QuestTestDataHash} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'build/private/game-data.verification.json')
@'
function Select-Quest([string]$SdkRoot,[string]$Serial,[switch]$NoDriverInstall) {
  [pscustomobject]@{serial='MOCK';model='Quest 3'}
}
function Invoke-QuestAdb([string[]]$Arguments,[switch]$AllowFailure) {
  $line=$Arguments -join ' '
  $global:QuestTestCalls.Add($line)
  $code=0;$output=''
  switch -Regex ($line) {
    'getprop ro.build.version.sdk$' {$output='35';break}
    'shell pm path ' {$output='package:/mock/base.apk';break}
    'shell ls -1 ' {$output="save.sav`nVR.ini";break}
    'shell df -k ' {
      $space=if($global:QuestTestScenario -eq 'no-space'){1}else{999999999}
      $output="Filesystem 1K-blocks Used Available Use% Mounted on`n/dev/mock 9999999999 100 $space 1% /sdcard";break
    }
    ' install ' {
      if ($global:QuestTestScenario -eq 'signature') { throw 'INSTALL_FAILED_UPDATE_INCOMPATIBLE' }
      $global:QuestTestInstalled=$true;$output='Success';break
    }
    'sha256sum /mock/base.apk' {$output=$global:QuestTestApkHash+'  /mock/base.apk';break}
    'sha256sum .*/files/(save.sav|VR.ini)' {
      $hash=if($global:QuestTestScenario -eq 'save-change' -and $global:QuestTestInstalled){'changed'}else{'preserved'}
      $output=$hash+'  file';break
    }
    'shell test -[ef] .*game-data-' {$code=1;break}
    'sha256sum .*/Download/Gothic2VR/' {
      $hash=if($global:QuestTestScenario -eq 'transfer-corrupt'){'corrupt'}else{$global:QuestTestDataHash}
      $output=$hash+'  file';break
    }
    'shell mkdir| push |shell mv ' {break}
    default {throw "Unexpected ADB command: $line"}
  }
  [pscustomobject]@{code=$code;output=$output}
}
'@ | Set-Content -LiteralPath (Join-Path $fixture 'tools/quest-common.ps1')
$results=@()
foreach ($scenario in @('update','dry-run','data','no-space','signature','save-change','transfer-corrupt','apk-corrupt')) {
  $global:QuestTestCalls=[Collections.Generic.List[string]]::new()
  $global:QuestTestScenario=$scenario;$global:QuestTestInstalled=$false
  if ($scenario -eq 'apk-corrupt') { Add-Content -LiteralPath (Join-Path $fixture 'test.apk') -Value 'tampered' }
  $failed=$false;$message=''
  try {
    & (Join-Path $fixture 'tools/install-quest.ps1') -DryRun:($scenario -eq 'dry-run') -WithGameData:($scenario -in @('data','transfer-corrupt')) | Out-Null
  } catch { $failed=$true;$message=$_.Exception.Message }
  $expectedFailure=$scenario -in @('no-space','signature','save-change','transfer-corrupt','apk-corrupt')
  if ($failed -ne $expectedFailure) { throw "Wrong result for ${scenario}: $message" }
  $calls=$global:QuestTestCalls -join "`n"
  if ($calls -match 'uninstall| am start ') { throw 'Unexpected uninstall or launch.' }
  if ($scenario -in @('dry-run','no-space','apk-corrupt') -and $calls -match ' install | push |shell mv ') { throw "Mutation before validation in $scenario" }
  if ($scenario -eq 'data' -and ($calls -notmatch ' push ' -or $calls -notmatch 'shell mv ')) { throw 'Successful data transfer did not commit.' }
  if ($scenario -eq 'transfer-corrupt' -and $calls -match 'shell mv ') { throw 'Corrupt transfer was committed.' }
  if ($scenario -eq 'save-change' -and $message -notmatch 'save/settings hash changed') { throw 'Settings preservation check failed.' }
  $results+=@{scenario=$scenario;passed=$true;expectedFailure=$expectedFailure}
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'report.json')
Write-Host "PASS: $($results.Count) installer scenarios; no physical device used. Report: $fixture/report.json"
