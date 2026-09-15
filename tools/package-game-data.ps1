#requires -Version 5.1
param([string]$GameDir, [switch]$Plan)
. (Join-Path $PSScriptRoot 'release-common.ps1')
$Python=Resolve-Python ''
if (!$GameDir) { $GameDir=(Read-Host 'Path to Gothic II Gold / Night of the Raven').Trim().Trim('"') }
$argsList=@('-B',(Join-Path $PSScriptRoot 'package-game-data.py'),'--game-root',$GameDir)
if ($Plan) { $argsList += '--plan' }
& $Python @argsList
if ($LASTEXITCODE -ne 0) { throw 'Game data packaging failed.' }
