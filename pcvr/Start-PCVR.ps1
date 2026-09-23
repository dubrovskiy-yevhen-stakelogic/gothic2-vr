param(
    [string]$GameRoot,
    [ValidateSet('Auto','SteamVR','Oculus','VirtualDesktop')][string]$Runtime = 'Auto',
    [string]$RuntimeManifest,
    [switch]$Info,
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Read-RegistryValue([string]$Key, [string]$Name) {
    $item = Get-ItemProperty -LiteralPath $Key -ErrorAction SilentlyContinue
    if ($item) { $item.$Name }
}

function Get-SteamLibraries {
    $roots = @(
        (Read-RegistryValue 'HKCU:\Software\Valve\Steam' 'SteamPath'),
        (Read-RegistryValue 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' 'InstallPath'),
        (Join-Path ${env:ProgramFiles(x86)} 'Steam')
    ) | Where-Object { $_ } | Select-Object -Unique
    foreach ($root in $roots) {
        $root
        $file = Join-Path $root 'steamapps/libraryfolders.vdf'
        if (Test-Path -LiteralPath $file) {
            foreach ($match in [regex]::Matches((Get-Content -LiteralPath $file -Raw), '"path"\s+"([^"]+)"')) {
                $match.Groups[1].Value.Replace('\\','\')
            }
        }
    }
}

function Resolve-XrManifest {
    if ($RuntimeManifest) { return $RuntimeManifest }
    if ($Runtime -eq 'Auto') {
        if ($env:XR_RUNTIME_JSON) { return $env:XR_RUNTIME_JSON }
        $active = Read-RegistryValue 'HKLM:\SOFTWARE\Khronos\OpenXR\1' 'ActiveRuntime'
        if (!$active) { throw 'No active OpenXR runtime. Use a runtime-specific launcher.' }
        return $active
    }
    $candidates = @()
    switch ($Runtime) {
        'SteamVR' {
            $pathsFile = Join-Path $env:LOCALAPPDATA 'openvr/openvrpaths.vrpath'
            if (Test-Path -LiteralPath $pathsFile) {
                try {
                    $paths = Get-Content -LiteralPath $pathsFile -Raw | ConvertFrom-Json
                    foreach ($dir in $paths.runtime) { $candidates += Join-Path $dir 'steamxr_win64.json' }
                } catch { Write-Host 'OpenVR path registry unreadable; checking Steam libraries.' }
            }
            foreach ($dir in Get-SteamLibraries) { $candidates += Join-Path $dir 'steamapps/common/SteamVR/steamxr_win64.json' }
        }
        'Oculus' {
            $base = Read-RegistryValue 'HKLM:\SOFTWARE\WOW6432Node\Oculus VR, LLC\Oculus' 'Base'
            if ($base) { $candidates += Join-Path $base 'Support/oculus-runtime/oculus_openxr_64.json' }
            foreach ($dir in @('Meta Horizon','Oculus')) {
                $candidates += Join-Path $env:ProgramFiles "$dir/Support/oculus-runtime/oculus_openxr_64.json"
            }
        }
        'VirtualDesktop' {
            $candidates += Join-Path $env:ProgramFiles 'Virtual Desktop Streamer/OpenXR/virtualdesktop-openxr.json'
        }
    }
    $available = Get-Item 'HKLM:\SOFTWARE\Khronos\OpenXR\1\AvailableRuntimes' -ErrorAction SilentlyContinue
    if ($available) {
        $pattern = switch ($Runtime) { 'SteamVR' {'steamxr_win64\.json$'} 'Oculus' {'oculus_openxr_64\.json$'} 'VirtualDesktop' {'virtualdesktop-openxr\.json$'} }
        $candidates += @($available.GetValueNames() | Where-Object { $_ -match $pattern })
    }
    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    throw "$Runtime x64 runtime not found. Install it, or pass -RuntimeManifest with its full JSON path. No other runtime was selected."
}

function Show-ConnectionHelp {
    switch ($Runtime) {
        'SteamVR' { Write-Host 'Connect the headset to SteamVR and wait until SteamVR reports it ready.' }
        'Oculus' { Write-Host 'Connect through Meta Quest Link / Air Link and enter the PC Link environment in the headset.' }
        'VirtualDesktop' { Write-Host 'Open Virtual Desktop in the headset and connect to this PC in Virtual Desktop Streamer.' }
        default { Write-Host 'Connect the headset through the runtime printed above, or choose a runtime-specific launcher.' }
    }
}

$savedRuntime = [Environment]::GetEnvironmentVariable('XR_RUNTIME_JSON','Process')
$result = 1
try {
    $manifestPath = (Resolve-Path -LiteralPath (Resolve-XrManifest)).Path
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $library = [string]$manifest.runtime.library_path
    if (!$library) { throw "Runtime manifest has no library_path: $manifestPath" }
    if (![IO.Path]::IsPathRooted($library)) { $library = Join-Path (Split-Path $manifestPath -Parent) $library }
    if (!(Test-Path -LiteralPath $library -PathType Leaf)) { throw "Runtime DLL missing: $library" }
    $exe = Join-Path $PSScriptRoot 'Gothic2Notr.exe'
    if (!(Test-Path -LiteralPath $exe)) { throw 'Gothic2Notr.exe missing. Extract the complete PCVR package.' }
    $env:XR_RUNTIME_JSON = $manifestPath
    Write-Host "OpenXR selection: $Runtime"
    Write-Host "Runtime manifest: $manifestPath"
    Write-Host 'This selection applies only to this launcher and its game process.'
    Show-ConnectionHelp
    if ($CheckOnly) {
        Write-Host 'Runtime JSON, runtime DLL and game EXE found. No runtime or game was started.'
        $result = 0
    } elseif ($Info) {
        & $exe -vrinfo
        $result = $LASTEXITCODE
        if ($result -ne 0) { Show-ConnectionHelp }
    } else {
        $cache = Join-Path $PSScriptRoot 'launcher-game-path.txt'
        if (!$GameRoot -and (Test-Path -LiteralPath $cache)) { $GameRoot = (Get-Content -LiteralPath $cache -Raw).Trim() }
        if (!$GameRoot) {
            $installed = @(foreach ($dir in Get-SteamLibraries) {
                $candidate = Join-Path $dir 'steamapps/common/Gothic II'
                if ((Test-Path -LiteralPath (Join-Path $candidate 'Data')) -and (Test-Path -LiteralPath (Join-Path $candidate 'System'))) { $candidate }
            }) | Select-Object -Unique
            if (@($installed).Count -eq 1) { $GameRoot = [string]$installed }
        }
        if (!$GameRoot) { $GameRoot = Read-Host 'Gothic II Gold / Night of the Raven folder (contains Data and System)' }
        $GameRoot = $GameRoot.Trim().Trim('"')
        if (!$GameRoot -or !(Test-Path -LiteralPath (Join-Path $GameRoot 'Data') -PathType Container) -or
            !(Test-Path -LiteralPath (Join-Path $GameRoot 'System') -PathType Container)) {
            throw 'Game folder not found. Pass -GameRoot with the folder containing Data and System.'
        }
        $GameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
        Set-Content -LiteralPath $cache -Value $GameRoot -Encoding UTF8
        Write-Host "Game folder: $GameRoot"
        & $exe -g $GameRoot
        $result = $LASTEXITCODE
        Write-Host "Game exited with code $result. Diagnostics: log.txt in this folder."
        if ($result -ne 0) { Show-ConnectionHelp }
    }
} catch {
    Write-Host "Launcher error: $($_.Exception.Message)" -ForegroundColor Red
    $result = 1
} finally {
    [Environment]::SetEnvironmentVariable('XR_RUNTIME_JSON',$savedRuntime,'Process')
}
exit $result
