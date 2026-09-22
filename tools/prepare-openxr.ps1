#requires -Version 5.1
[CmdletBinding()]
param([string]$Python, [switch]$VerifyOnly, [switch]$Windows, [switch]$RecordHashes)
. (Join-Path $PSScriptRoot 'release-common.ps1')
$Python = Resolve-Python $Python
$component = if ($Windows) { 'openxr-windows' } else { 'openxr' }
$arguments = @((Join-Path $PSScriptRoot 'bootstrap-dependencies.py'), '--component', $component)
if ($VerifyOnly) { $arguments += '--verify-only' }
if ($RecordHashes) {
  if (!$Windows) { throw '-RecordHashes applies to the Windows OpenXR loader only; pass -Windows.' }
  $arguments += '--record-hashes'
}
& $Python @arguments
if ($LASTEXITCODE -ne 0) { throw 'OpenXR dependency preparation failed.' }
