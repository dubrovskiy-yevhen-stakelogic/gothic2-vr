# Requires Windows and locally installed SteamVR, Meta Link and Virtual Desktop runtimes.
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path (Join-Path $sourceRoot 'build') ('launcher-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
Copy-Item "$sourceRoot\pcvr\Start-PCVR.ps1" $fixture
$code = @'
using System;
using System.IO;
class Probe {
  static int Main(string[] args) {
    File.WriteAllLines("child-environment.txt", new string[] {
      Environment.GetEnvironmentVariable("XR_RUNTIME_JSON") ?? "", String.Join("|", args)
    });
    return 0;
  }
}
'@
Add-Type -TypeDefinition $code -OutputAssembly "$fixture\Gothic2Notr.exe" -OutputType ConsoleApplication
$regBefore = (Get-ItemProperty 'HKLM:\SOFTWARE\Khronos\OpenXR\1').ActiveRuntime
$savedEnv = $env:XR_RUNTIME_JSON
$results = @()
try {
    $env:XR_RUNTIME_JSON = 'C:\deliberately-wrong-inherited-runtime.json'
    foreach ($choice in @('SteamVR','Oculus','VirtualDesktop')) {
        $output = & powershell -NoProfile -ExecutionPolicy Bypass -File "$fixture\Start-PCVR.ps1" -Runtime $choice -Info 2>&1
        if ($LASTEXITCODE -ne 0) { throw "$choice failed: $output" }
        $child = Get-Content "$fixture\child-environment.txt"
        $expected = switch ($choice) { SteamVR {'steamxr_win64.json'} Oculus {'oculus_openxr_64.json'} VirtualDesktop {'virtualdesktop-openxr.json'} }
        if ([IO.Path]::GetFileName($child[0]) -ne $expected -or $child[1] -ne '-vrinfo') { throw "Wrong child state: $child" }
        $results += "PASS $choice overrides inherited selection in child process"
    }
    $game = Join-Path $fixture 'Game folder with spaces'
    New-Item -ItemType Directory -Path "$game\Data","$game\System" | Out-Null
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$fixture\Start-PCVR.ps1" -Runtime SteamVR -GameRoot $game | Out-Null
    if ($LASTEXITCODE -ne 0 -or (Get-Content "$fixture\child-environment.txt")[1] -ne "-g|$game") { throw 'Game arguments not preserved' }
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$fixture\Start-PCVR.ps1" -Runtime Oculus | Out-Null
    if ($LASTEXITCODE -ne 0 -or (Get-Content "$fixture\child-environment.txt")[1] -ne "-g|$game") { throw 'Saved game path not reused' }
    $results += 'PASS game path with spaces and local saved-path reuse'
    $before = (Get-FileHash "$fixture\child-environment.txt").Hash
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$fixture\Start-PCVR.ps1" -RuntimeManifest "$fixture\missing.json" -Info | Out-Null
    if ($LASTEXITCODE -eq 0 -or (Get-FileHash "$fixture\child-environment.txt").Hash -ne $before) { throw 'Missing manifest did not fail closed' }
    $results += 'PASS missing manifest does not launch child or fall back'
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$fixture\Start-PCVR.ps1" -Runtime SteamVR -CheckOnly | Out-Null
    if ($LASTEXITCODE -ne 0 -or (Get-FileHash "$fixture\child-environment.txt").Hash -ne $before) { throw 'CheckOnly launched child' }
    $results += 'PASS CheckOnly does not launch child'
    if ($env:XR_RUNTIME_JSON -ne 'C:\deliberately-wrong-inherited-runtime.json' -or (Get-ItemProperty 'HKLM:\SOFTWARE\Khronos\OpenXR\1').ActiveRuntime -ne $regBefore) { throw 'Parent environment or system setting changed' }
    $results += 'PASS parent environment and system ActiveRuntime unchanged'
} finally { $env:XR_RUNTIME_JSON = $savedEnv }
$results | Set-Content (Join-Path $fixture "results.txt")
$results
'Tests used a stub executable; no game or OpenXR runtime was started.'
