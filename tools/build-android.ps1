#requires -Version 5.1
[CmdletBinding()]
param([string]$SdkRoot, [string]$JavaRoot, [string]$Python, [string]$GradleHome, [switch]$Offline)
. (Join-Path $PSScriptRoot 'release-common.ps1')
if ($Offline) { $env:GOTHIC2VR_OFFLINE='1' }
$SdkRoot = Resolve-AndroidSdk $SdkRoot
$JavaRoot = Resolve-JavaRoot $JavaRoot
$Python = Resolve-Python $Python
Initialize-AndroidBuildTools $SdkRoot $JavaRoot
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$lock = Get-Content -LiteralPath (Join-Path $root 'config/android-toolchain.lock.json') -Raw | ConvertFrom-Json
$buildRoot = Join-Path $root 'build/android'
if (!$GradleHome) { $GradleHome = Join-Path $root 'toolchain/gradle-home' }
$gradleHome = [IO.Path]::GetFullPath($GradleHome)
$cmake = Join-Path $SdkRoot ('cmake/' + $lock.cmakeVersion + '/bin/cmake.exe')
$ninja = Join-Path $SdkRoot ('cmake/' + $lock.cmakeVersion + '/bin/ninja.exe')
$keytool = Join-Path $JavaRoot 'bin/keytool.exe'
$signer = Join-Path $SdkRoot ('build-tools/' + $lock.buildToolsVersion + '/apksigner.bat')
$dependencyInit = Join-Path $root 'toolchain/dependencies/source-dependencies.cmake'
foreach ($path in @($cmake, $ninja, $keytool, $signer,
    (Join-Path $SdkRoot ('ndk/' + $lock.ndkVersion + '/source.properties')),
    (Join-Path $SdkRoot ('platforms/android-' + $lock.compileSdk + '/android.jar')))) {
  if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required build dependency missing: $path. See BUILDING.md." }
}
$depArgs = @('-B',(Join-Path $PSScriptRoot 'bootstrap-dependencies.py'))
if ($Offline) { $depArgs += '--verify-only' }
& $Python @depArgs
if ($LASTEXITCODE -ne 0) { throw 'Source dependency setup failed.' }

function Invoke-BuildTool([string]$Executable, [string[]]$Arguments, [string]$Log) {
  # Windows PowerShell 5.1 wraps native stderr in ErrorRecord even for successful tools.
  $ErrorActionPreference = 'Continue'
  & $Executable @Arguments 2>&1 | ForEach-Object { "$PSItem" } | Tee-Object -FilePath $Log
  $nativeCode = $LASTEXITCODE
  if ($nativeCode -ne 0) { throw "Build command failed with exit $nativeCode. Log: $Log" }
}

function Read-SourceHashes {
  foreach ($directory in @('engine', 'android', 'config', 'tools')) {
    Get-ChildItem -LiteralPath (Join-Path $root $directory) -Recurse -File |
      Where-Object { $_.FullName -notmatch '[\\/](__pycache__|\.git|\.cache)[\\/]' -and $_.Extension -ne '.pyc' } |
      Sort-Object FullName | ForEach-Object {
        [ordered]@{path=$_.FullName.Substring($root.Length + 1).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
      }
  }
}
$sourceHashes = @(Read-SourceHashes)
$null = New-Item -ItemType Directory -Path $buildRoot, $gradleHome -Force
$logRoot = Join-Path $root ('logs/android-build-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
$null = New-Item -ItemType Directory -Path $logRoot
$keystore = $env:GOTHIC2VR_KEYSTORE
$localSigning = !$keystore
if ($localSigning) { $keystore = Join-Path $root 'toolchain/android-debug.keystore' }
else {
  if (!(Test-Path -LiteralPath $keystore -PathType Leaf)) { throw 'GOTHIC2VR_KEYSTORE must identify an existing signing key.' }
  foreach ($name in @('GOTHIC2VR_KEY_ALIAS','GOTHIC2VR_STORE_PASSWORD','GOTHIC2VR_KEY_PASSWORD')) {
    if (![Environment]::GetEnvironmentVariable($name,'Process')) { throw "Set $name for the selected signing key." }
  }
}
$environment = @{
  JAVA_HOME=$JavaRoot; ANDROID_HOME=$SdkRoot; ANDROID_SDK_ROOT=$SdkRoot;
  GRADLE_USER_HOME=$gradleHome; ANDROID_USER_HOME=(Join-Path $root 'toolchain/android-user');
  GOTHIC2VR_KEYSTORE=$keystore
}
if ($localSigning) {
  $environment['GOTHIC2VR_KEY_ALIAS']='androiddebugkey'
  $environment['GOTHIC2VR_STORE_PASSWORD']='android'
  $environment['GOTHIC2VR_KEY_PASSWORD']='android'
}
$savedEnvironment = @{}
foreach ($name in $environment.Keys) {
  $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name,'Process')
  [Environment]::SetEnvironmentVariable($name,$environment[$name],'Process')
}
try {
  if ($localSigning -and !(Test-Path -LiteralPath $keystore)) {
    Invoke-BuildTool $keytool @('-genkeypair','-keystore',$keystore,'-storepass','android','-keypass','android','-alias','androiddebugkey','-keyalg','RSA','-keysize','2048','-validity','10000','-dname','CN=Gothic2VR Local Development') (Join-Path $logRoot 'key-generation.log')
  }
  $configure = @('-S',(Join-Path $root 'android'),'-B',$buildRoot,'-G','Ninja',
    "-DCMAKE_MAKE_PROGRAM=$ninja", "-DGOTHIC2VR_DEPENDENCY_INIT=$($dependencyInit.Replace('\','/'))",
    "-DTEMPEST_ANDROID_NDK=$($lock.ndkVersion)", "-DTEMPEST_ANDROID_MIN_SDK=$($lock.minSdk)",
    "-DTEMPEST_ANDROID_CMAKE=$($lock.cmakeVersion)", "-DTEMPEST_ANDROID_COMPILE_SDK=$($lock.compileSdk)",
    "-DTEMPEST_ANDROID_BUILD_TOOLS=$($lock.buildToolsVersion)", "-DTEMPEST_ANDROID_AGP=$($lock.agpVersion)",
    "-DTEMPEST_ANDROID_ABIS=$($lock.abi)",'-DTEMPEST_ANDROID_BUILD_TYPE=Release')
  Invoke-BuildTool $cmake $configure (Join-Path $logRoot 'configure.log')
  $generated = Join-Path $buildRoot 'Gothic2VR'
  $wrapperProperties = Get-Content -LiteralPath (Join-Path $generated 'gradle/wrapper/gradle-wrapper.properties') -Raw
  if ($wrapperProperties -notmatch [regex]::Escape($lock.gradleSha256) -or $wrapperProperties -notmatch ('gradle-' + [regex]::Escape($lock.gradleVersion) + '-bin.zip')) { throw 'Gradle wrapper differs from lock.' }
  $gradleArguments = @('-p',$generated,'--no-daemon','--max-workers=2','--console=plain','assembleRelease','lintRelease')
  if ($Offline) {
    $distributionRoot=Join-Path $gradleHome ('wrapper/dists/gradle-'+$lock.gradleVersion+'-bin')
    $ready=@(Get-ChildItem -LiteralPath $distributionRoot -Recurse -File -Filter ('gradle-'+$lock.gradleVersion+'-bin.zip.ok') -ErrorAction SilentlyContinue)
    if ($ready.Count -eq 0) { throw 'Offline mode requires a previously downloaded Gradle distribution. Run BUILD-APK.bat once with internet access.' }
    $gradleArguments += '--offline'
  }
  Invoke-BuildTool (Join-Path $generated 'gradlew.bat') $gradleArguments (Join-Path $logRoot 'gradle.log')
  $apk = Join-Path $generated 'app/build/outputs/apk/release/app-release.apk'
  if (!(Test-Path -LiteralPath $apk -PathType Leaf)) { throw 'Expected APK missing.' }
  Invoke-BuildTool $signer @('verify','--verbose','--print-certs',$apk) (Join-Path $logRoot 'signature.log')
  $after = @(Read-SourceHashes)
  if (($sourceHashes | ConvertTo-Json -Depth 4 -Compress) -cne ($after | ConvertTo-Json -Depth 4 -Compress)) { throw 'Source files changed during the build; APK retained but not accepted. Build again after edits finish.' }
  $manifest = [ordered]@{
    schemaVersion=1;status='built';completedUtc=[DateTime]::UtcNow.ToString('o');applicationId=$lock.applicationId;
    architecture=$lock.abi;variant='Release';renderer='OpenXR Vulkan stereo';logs=$logRoot;
    apk=[ordered]@{path=$apk;bytes=(Get-Item -LiteralPath $apk).Length;sha256=(Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash};
    sourceFiles=$sourceHashes;toolchain=$lock;sdkRoot=$SdkRoot;javaRoot=$JavaRoot;localDevelopmentSigning=$localSigning;
    deviceInstall=$false;deviceLaunch=$false;headsetValidated=$false
  }
  $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $buildRoot 'build-manifest.json') -Encoding UTF8
  Invoke-BuildTool $Python @((Join-Path $PSScriptRoot 'verify-android.py'),'--sdk-root',$SdkRoot) (Join-Path $logRoot 'verification.log')
  Write-Host "Built and verified: $apk"
} finally {
  foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process') }
}
