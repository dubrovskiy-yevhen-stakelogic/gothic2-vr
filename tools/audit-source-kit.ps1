#requires -Version 5.1
. (Join-Path $PSScriptRoot 'release-common.ps1')
$Python=Resolve-Python ''
& $Python -B (Join-Path $PSScriptRoot 'audit-source-kit.py') --allow-local-state
if ($LASTEXITCODE -ne 0) { throw 'Source integrity audit failed.' }
