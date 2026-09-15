#requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:ReleaseRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Get-PortableDependency([string]$Name) {
  $lock = Get-Content -LiteralPath (Join-Path $script:ReleaseRoot 'config/bootstrap.lock.json') -Raw | ConvertFrom-Json
  $entry = $lock.$Name
  if ($entry.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or $entry.url -notmatch '^https://') { throw "Invalid dependency lock: $Name" }
  $local = Join-Path $script:ReleaseRoot 'toolchain'
  $destination = Join-Path $local $entry.directory
  $marker = Join-Path $destination ('.verified-' + $Name)
  if ((Test-Path -LiteralPath $marker) -and (Get-Content -LiteralPath $marker -Raw).Trim() -eq $entry.sha256) { return $destination }
  if ($env:GOTHIC2VR_OFFLINE -eq '1') { throw "Offline dependency missing: $Name. Run SETUP-DEPENDENCIES.bat with internet access first." }
  $cache = Join-Path $local 'downloads'
  $null = New-Item -ItemType Directory -Path $cache,$destination -Force
  $archive = Join-Path $cache ($Name + '-' + $entry.sha256.Substring(0,12) + '.zip')
  if (!(Test-Path -LiteralPath $archive)) {
    Write-Host "Downloading $Name from $($entry.url)"
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -UseBasicParsing -Uri $entry.url -OutFile ($archive + '.part')
    if ((Get-FileHash -LiteralPath ($archive + '.part') -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Download checksum mismatch: $Name" }
    Move-Item -LiteralPath ($archive + '.part') -Destination $archive
  }
  if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Cached archive checksum mismatch: $Name" }
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $zip = [IO.Compression.ZipFile]::OpenRead($archive)
  try {
    $prefix = [IO.Path]::GetFullPath($destination).TrimEnd('\') + '\'
    foreach ($item in $zip.Entries) {
      $target = [IO.Path]::GetFullPath((Join-Path $destination $item.FullName))
      if (!$target.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe dependency archive path.' }
      if (!$item.Name) { $null = New-Item -ItemType Directory -Path $target -Force; continue }
      $null = New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force
      [IO.Compression.ZipFileExtensions]::ExtractToFile($item,$target,$true)
    }
  } finally { $zip.Dispose() }
  Set-Content -LiteralPath $marker -Value $entry.sha256 -Encoding ASCII
  return $destination
}

function Resolve-AndroidSdk([string]$SdkRoot) {
  if (!$SdkRoot) { $SdkRoot = $env:ANDROID_SDK_ROOT }
  if (!$SdkRoot) { $SdkRoot = $env:ANDROID_HOME }
  if (!$SdkRoot) { $SdkRoot = Get-PortableDependency 'adb' }
  if (!(Test-Path -LiteralPath (Join-Path $SdkRoot 'platform-tools/adb.exe'))) { throw "ADB missing in specified SDK: $SdkRoot" }
  return (Get-Item -LiteralPath $SdkRoot).FullName
}

function Resolve-JavaRoot([string]$JavaRoot) {
  if (!$JavaRoot -and $env:JAVA_HOME -and (Test-Path -LiteralPath (Join-Path $env:JAVA_HOME 'release'))) {
    if ((Get-Content -LiteralPath (Join-Path $env:JAVA_HOME 'release') -Raw) -match 'JAVA_VERSION="21(?:\.|"|\+)') { $JavaRoot=$env:JAVA_HOME }
  }
  if (!$JavaRoot) {
    $folder = Get-PortableDependency 'java'
    $JavaRoot = (Get-ChildItem -LiteralPath $folder -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin/java.exe') } | Select-Object -First 1).FullName
  }
  if (!(Test-Path -LiteralPath (Join-Path $JavaRoot 'bin/java.exe'))) { throw "JDK missing: $JavaRoot" }
  if ((Get-Content -LiteralPath (Join-Path $JavaRoot 'release') -Raw) -notmatch 'JAVA_VERSION="21(?:\.|"|\+)') { throw 'JDK 21 is required.' }
  return (Get-Item -LiteralPath $JavaRoot).FullName
}

function Resolve-Python([string]$Python) {
  if (!$Python) { $Python = Join-Path (Get-PortableDependency 'python') 'python.exe' }
  $command = Get-Command $Python -ErrorAction SilentlyContinue
  if (!$command -or $command.Source -match '\\WindowsApps\\') { throw "Python executable missing: $Python" }
  & $command.Source -c 'import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)'
  if ($LASTEXITCODE -ne 0) { throw 'Python 3.10 or newer is required.' }
  return $command.Source
}

function Initialize-AndroidBuildTools([string]$SdkRoot, [string]$JavaRoot) {
  $lock = Get-Content -LiteralPath (Join-Path $script:ReleaseRoot 'config/android-toolchain.lock.json') -Raw | ConvertFrom-Json
  $components = [ordered]@{
    'platform-tools'='platform-tools/adb.exe'
    "platforms;android-$($lock.compileSdk)"="platforms/android-$($lock.compileSdk)/android.jar"
    "build-tools;$($lock.buildToolsVersion)"="build-tools/$($lock.buildToolsVersion)/apksigner.bat"
    "ndk;$($lock.ndkVersion)"="ndk/$($lock.ndkVersion)/source.properties"
    "cmake;$($lock.cmakeVersion)"="cmake/$($lock.cmakeVersion)/bin/cmake.exe"
  }
  $missing = @($components.Keys | Where-Object { !(Test-Path -LiteralPath (Join-Path $SdkRoot $components[$_])) })
  if (!$missing.Count) { return }
  if ($env:GOTHIC2VR_OFFLINE -eq '1') { throw "Offline SDK components missing: $($missing -join ', ')" }
  $manager = Join-Path (Get-PortableDependency 'commandline') 'cmdline-tools/bin/sdkmanager.bat'
  $oldJava = $env:JAVA_HOME
  try {
    $env:JAVA_HOME=$JavaRoot
    Write-Host 'Review and accept the Android SDK licenses to install the build tools.'
    & $manager "--sdk_root=$SdkRoot" --licenses
    if ($LASTEXITCODE -ne 0) { throw 'Android SDK license setup failed.' }
    & $manager "--sdk_root=$SdkRoot" @missing
    if ($LASTEXITCODE -ne 0) { throw 'Android SDK component installation failed.' }
  } finally { $env:JAVA_HOME=$oldJava }
  foreach ($part in $components.Keys) {
    if (!(Test-Path -LiteralPath (Join-Path $SdkRoot $components[$part]))) { throw "SDK component missing after setup: $part" }
  }
}
