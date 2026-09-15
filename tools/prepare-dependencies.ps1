#requires -Version 5.1
[CmdletBinding()]
param([string]$Python, [switch]$VerifyOnly)
. (Join-Path $PSScriptRoot 'release-common.ps1')
$Python = Resolve-Python $Python
$arguments = @((Join-Path $PSScriptRoot 'bootstrap-dependencies.py'), '--component', 'vulkan')
if ($VerifyOnly) { $arguments += '--verify-only' }
& $Python @arguments
if ($LASTEXITCODE -ne 0) { throw 'Vulkan dependency preparation failed.' }
