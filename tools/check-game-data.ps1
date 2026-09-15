#requires -Version 5.1
<#!
.SYNOPSIS
Read-only, quick preflight for the owned Gothic II: Night of the Raven data.
.DESCRIPTION
Reads archive headers/catalogs and loose-script metadata, never archive payloads.
No game launch, extraction, copy, or write to the installation is performed.
The optional report must be outside the game installation.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GameRoot,
    [string]$OutputPath,
    [switch]$AsObject
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$GameRoot = (Get-Item -LiteralPath $GameRoot -ErrorAction Stop).FullName.TrimEnd('\', '/')
if (-not (Test-Path -LiteralPath $GameRoot -PathType Container)) { throw "GameRoot is not a directory: $GameRoot" }

function Read-ArchiveCatalog([IO.FileInfo]$Archive) {
    $stream = [IO.File]::Open($Archive.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 296) { throw "Short VDF header: $($Archive.Name)" }
        $stream.Position = 256
        $signature = [Text.Encoding]::ASCII.GetString($reader.ReadBytes(16))
        if (-not $signature.StartsWith('PSVDSC_V2.00')) { throw "Unsupported VDF signature: $($Archive.Name)" }
        $entryCount = $reader.ReadUInt32()
        $fileCount = $reader.ReadUInt32()
        $timestamp = $reader.ReadUInt32()
        $null = $reader.ReadUInt32() # Declared volume size.
        $catalogOffset = $reader.ReadUInt32()
        $volumeFlags = $reader.ReadUInt32()
        if ($catalogOffset -eq 0) { $catalogOffset = 296 }
        if ($entryCount -gt 1000000 -or ([long]$catalogOffset + [long]$entryCount * 80) -gt $stream.Length) {
            throw "Invalid VDF catalog bounds: $($Archive.Name)"
        }
        $stream.Position = $catalogOffset
        $catalog = $reader.ReadBytes([int]($entryCount * 80))
        if ($catalog.Length -ne [int]($entryCount * 80)) { throw "Short VDF catalog: $($Archive.Name)" }
        $hasher = [Security.Cryptography.SHA256]::Create()
        try { $catalogHash = ([BitConverter]::ToString($hasher.ComputeHash($catalog))).Replace('-', '') }
        finally { $hasher.Dispose() }
        $worlds = [Collections.Generic.List[object]]::new()
        $filesFound = 0
        for ($index = 0; $index -lt $entryCount; $index++) {
            $offset = $index * 80
            $name = [Text.Encoding]::ASCII.GetString($catalog, $offset, 64).TrimEnd([char[]]@(0, 32, 9, 10, 13))
            $entryOffset = [BitConverter]::ToUInt32($catalog, $offset + 64)
            $bytes = [BitConverter]::ToUInt32($catalog, $offset + 68)
            $flags = [BitConverter]::ToUInt32($catalog, $offset + 72)
            if (($flags -band 2147483648) -ne 0) { continue }
            $filesFound++
            if ($volumeFlags -eq 80 -and ([long]$entryOffset + $bytes) -gt $stream.Length) {
                throw "Invalid VDF payload bounds: $($Archive.Name)/$name"
            }
            if ([IO.Path]::GetExtension($name) -ieq '.zen') {
                $worlds.Add([pscustomobject]@{ name = $name; bytes = $bytes })
            }
        }
        if ($filesFound -ne $fileCount) { throw "VDF file count mismatch: $($Archive.Name)" }
        [pscustomobject]@{
            name = $Archive.Name
            bytes = $Archive.Length
            last_write_utc = $Archive.LastWriteTimeUtc.ToString('o')
            files = $fileCount
            timestamp_dos_raw = $timestamp
            catalog_sha256 = $catalogHash
            catalog_bytes_read = $catalog.Length
            worlds = @($worlds.ToArray())
        }
    } finally { $reader.Dispose(); $stream.Dispose() }
}

$requiredDirectories = @('Data', '_work\Data', '_work\Data\Scripts\_compiled', '_work\Data\Scripts\CONTENT\CUTSCENE')
foreach ($relativePath in $requiredDirectories) {
    if (-not (Test-Path -LiteralPath (Join-Path $GameRoot $relativePath) -PathType Container)) {
        throw "Missing game-data directory: $relativePath"
    }
}
$requiredArchives = @('Anims.vdf', 'Anims_Addon.vdf', 'Meshes.vdf', 'Meshes_Addon.vdf', 'Textures.vdf', 'Textures_Addon.vdf', 'Sounds.vdf', 'Sounds_Addon.vdf', 'Worlds.vdf', 'Worlds_Addon.vdf')
$archiveFiles = @(Get-ChildItem -LiteralPath (Join-Path $GameRoot 'Data') -File | Where-Object { $_.Extension -ieq '.vdf' } | Sort-Object Name)
foreach ($name in $requiredArchives) {
    if ($name -notin $archiveFiles.Name) { throw "Missing Gothic II / NotR archive: Data\$name" }
}
$archives = @($archiveFiles | ForEach-Object { Read-ArchiveCatalog $_ })
$worldNames = @($archives | ForEach-Object { $_.worlds } | ForEach-Object { $_.name })
foreach ($name in @('NEWWORLD.ZEN', 'OLDWORLD.ZEN', 'DRAGONISLAND.ZEN', 'ADDONWORLD.ZEN')) {
    if ($name -notin $worldNames) { throw "Required Gothic II / NotR world missing from VDF catalogs: $name" }
}
$scripts = @('GOTHIC.DAT', 'MENU.DAT', 'SFX.DAT', 'PARTICLEFX.DAT', 'VISUALFX.DAT', 'FIGHT.DAT', 'CAMERA.DAT', 'MUSIC.DAT') | ForEach-Object {
    $relativePath = Join-Path '_work\Data\Scripts\_compiled' $_
    $file = Get-Item -LiteralPath (Join-Path $GameRoot $relativePath) -ErrorAction Stop
    if ($file.PSIsContainer -or $file.Length -eq 0) { throw "Empty or invalid compiled script: $relativePath" }
    [pscustomobject]@{ path = $relativePath; bytes = $file.Length; last_write_utc = $file.LastWriteTimeUtc.ToString('o') }
}
$outputUnits = @('OU.DAT', 'OU.BIN') | ForEach-Object {
    $relativePath = Join-Path '_work\Data\Scripts\CONTENT\CUTSCENE' $_
    $file = Get-Item -LiteralPath (Join-Path $GameRoot $relativePath) -ErrorAction SilentlyContinue
    if ($null -ne $file -and -not $file.PSIsContainer -and $file.Length -gt 0) {
        [pscustomobject]@{ path = $relativePath; bytes = $file.Length; last_write_utc = $file.LastWriteTimeUtc.ToString('o') }
    }
}
if (@($outputUnits).Count -eq 0) { throw 'No non-empty OU.DAT or OU.BIN dialogue output-unit file found.' }
$retailSettings = @('system\Gothic.ini', 'system\SystemPack.ini') | ForEach-Object {
    $file = Get-Item -LiteralPath (Join-Path $GameRoot $_) -ErrorAction SilentlyContinue
    if ($null -ne $file) { [pscustomobject]@{ path = $_; bytes = $file.Length; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash } }
}
$report = [pscustomobject][ordered]@{
    schema_version = 1
    inspected_at_utc = [DateTimeOffset]::UtcNow.ToString('o')
    status = 'passed'
    game = 'Gothic II: Night of the Raven'
    game_root = $GameRoot
    method = 'Read-only VDF headers/catalog hashes and loose file metadata. Archive payloads are not hashed or extracted. This verifies presence/catalog integrity, not runtime playability.'
    archive_count = $archives.Count
    archive_catalog_bytes_read = ($archives | Measure-Object catalog_bytes_read -Sum).Sum
    archives = $archives
    compiled_scripts = @($scripts)
    dialogue_output_units = @($outputUnits)
    retail_settings = @($retailSettings)
}
if ($OutputPath) {
    $reportPath = [IO.Path]::GetFullPath($OutputPath)
    if ($reportPath.Equals($GameRoot, [StringComparison]::OrdinalIgnoreCase) -or $reportPath.StartsWith($GameRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The preflight report must be outside the retail installation.'
    }
    # Reject path aliases so an apparently external report cannot write through a junction.
    $ancestor = $reportPath
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            $item = Get-Item -LiteralPath $ancestor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Report path contains a reparse point: $ancestor" }
            $linkType = $item.PSObject.Properties['LinkType']
            if ($linkType -and $linkType.Value -eq 'HardLink') { throw "Report path is a hard link: $ancestor" }
        }
        $ancestor = Split-Path -Parent $ancestor
    }
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $reportPath) -Force
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
}
if ($AsObject) { $report } else { $report | ConvertTo-Json -Depth 8 }
