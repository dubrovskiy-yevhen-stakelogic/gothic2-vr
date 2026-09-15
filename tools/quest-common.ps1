#requires -Version 5.1
. (Join-Path $PSScriptRoot 'release-common.ps1')

function Invoke-QuestAdb([string[]]$Arguments, [switch]$AllowFailure) {
  $ErrorActionPreference='Continue'
  $lines=@(& $script:adb @Arguments 2>&1 | ForEach-Object { "$PSItem" })
  $code=$LASTEXITCODE
  if ($code -ne 0 -and !$AllowFailure) { throw "ADB failed ($code): $($lines -join [Environment]::NewLine)" }
  [pscustomobject]@{code=$code;output=($lines -join [Environment]::NewLine).Trim()}
}

function Select-Quest([string]$SdkRoot, [string]$Serial, [switch]$NoDriverInstall) {
  $resolvedSdk=Resolve-AndroidSdk $SdkRoot
  $script:adb=Join-Path $resolvedSdk 'platform-tools/adb.exe'
  if (!(Test-Path -LiteralPath $script:adb -PathType Leaf)) { throw 'Install Android SDK platform-tools (adb.exe).' }
  $devices=Invoke-QuestAdb @('devices')
  $online=@($devices.output -split '\r?\n' | Where-Object { $_ -match '^([^\s]+)\s+device$' } | ForEach-Object { ($_ -split '\s+')[0] })
  if (!$online.Count -and !$NoDriverInstall -and $devices.output -notmatch 'unauthorized|offline') {
    $missingDriver=@(Get-CimInstance Win32_PnPEntity -Filter 'ConfigManagerErrorCode=28' -ErrorAction SilentlyContinue |
      Where-Object { $_.PNPDeviceID -match '^USB\\VID_2833&' })
    if ($missingDriver.Count) {
      $driver=Join-Path (Get-PortableDependency 'usbdriver') 'oculus-go-adb-driver-2.0/usb_driver/android_winusb.inf'
      Write-Host 'Quest USB driver is missing. Windows will request administrator permission to install the official Meta driver.'
      $process=Start-Process -FilePath (Join-Path $env:SystemRoot 'System32/pnputil.exe') -ArgumentList @('/add-driver',('"'+$driver+'"'),'/install') -Verb RunAs -WindowStyle Hidden -Wait -PassThru
      if ($process.ExitCode -notin @(0,3010)) { throw "Meta USB driver installation failed: $($process.ExitCode)" }
      if ($process.ExitCode -eq 3010) { throw 'Windows requires a restart after USB driver setup. Restart and run INSTALL.bat again.' }
      $devices=Invoke-QuestAdb @('devices')
      $online=@($devices.output -split '\r?\n' | Where-Object { $_ -match '^([^\s]+)\s+device$' } | ForEach-Object { ($_ -split '\s+')[0] })
    }
  }
  if (!$Serial) {
    if ($online.Count -ne 1) { throw 'Connect and authorize one Quest, or pass -Serial when multiple devices are connected.' }
    $Serial=$online[0]
  }
  if ($Serial -notin $online) { throw 'Selected Quest is not connected and authorized.' }
  $model=(Invoke-QuestAdb @('-s',$Serial,'shell','getprop','ro.product.model')).output
  if ($model -notmatch '^Quest') { throw "Selected device is not a Quest: $model" }
  [pscustomobject]@{serial=$Serial;model=$model}
}
