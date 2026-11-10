[CmdletBinding()]
param(
    [string]$PackageRoot,
    [string]$ObsRoot,
    [string]$PluginRoot,
    [switch]$Portable,
    [switch]$MigrateLegacy
)
$ErrorActionPreference = 'Stop'

if (Get-Process -Name obs64, obs32 -ErrorAction SilentlyContinue) {
    throw 'Close OBS Studio before installing or updating Bacons Helper.'
}
if (-not $PackageRoot) {
    if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'bacons-helper')) { $PackageRoot = $PSScriptRoot }
    else { $PackageRoot = Join-Path $PSScriptRoot '..\dist\windows-x64' }
}
$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
$Source = Join-Path $PackageRoot 'bacons-helper'
foreach ($Relative in @('bacons-helper.dll', 'data\manifest.json', 'data\locale\en-US.ini', 'data\language-guesser\model.json', 'data\qt-plugins\tls\qschannelbackend.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $Source $Relative) -PathType Leaf)) { throw "Incomplete plugin bundle: $Relative" }
}
$Metadata = Get-Content -LiteralPath (Join-Path $Source 'data\manifest.json') -Raw | ConvertFrom-Json
if ($Metadata.id -ne 'bacons-helper' -or -not $Metadata.display_name -or -not $Metadata.version) { throw 'Invalid Bacons Helper plugin manager manifest.' }
if ((Get-Item -LiteralPath $Source).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Plugin bundle must not be a symbolic link.' }
if (Get-ChildItem -LiteralPath $Source -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { throw 'Plugin bundle contains symbolic links.' }

if ($Portable) {
    if (-not $ObsRoot -or -not (Test-Path -LiteralPath (Join-Path $ObsRoot 'bin\64bit\obs64.exe'))) { throw 'Portable installation requires -ObsRoot pointing to an OBS installation.' }
    $ObsRoot = (Resolve-Path -LiteralPath $ObsRoot).Path
    if (-not $PluginRoot) { $PluginRoot = Join-Path $ObsRoot 'plugins' }
} elseif (-not $PluginRoot) {
    $PluginRoot = Join-Path $env:ProgramData 'obs-studio\plugins'
}
$PluginRoot = [IO.Path]::GetFullPath($PluginRoot)
$Target = Join-Path $PluginRoot 'bacons-helper'
if ([IO.Path]::GetFullPath($Source).TrimEnd('\') -eq $Target.TrimEnd('\')) { throw 'Source and installed plugin are the same directory. Extract the release into a separate folder first.' }

$Stamp = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N')
$Backup = Join-Path (Split-Path -Parent $PluginRoot) "plugin-backups\bacons-helper\$Stamp"
$Staging = Join-Path $PluginRoot ('.bacons-helper-stage-' + [Guid]::NewGuid().ToString('N'))
$Previous = Join-Path $Backup 'previous-bundle'
$MovedPrevious = $false
try {
    New-Item -ItemType Directory -Path $PluginRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $Staging | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | Copy-Item -Destination $Staging -Recurse -Force
    if (Test-Path -LiteralPath $Target) {
        New-Item -ItemType Directory -Path $Backup -Force | Out-Null
        Move-Item -LiteralPath $Target -Destination $Previous
        $MovedPrevious = $true
    }
    Move-Item -LiteralPath $Staging -Destination $Target
} catch {
    if ($MovedPrevious -and -not (Test-Path -LiteralPath $Target)) { Move-Item -LiteralPath $Previous -Destination $Target }
    throw
} finally {
    if (Test-Path -LiteralPath $Staging) { Remove-Item -LiteralPath $Staging -Recurse -Force }
}

if ($MigrateLegacy) {
    if (-not $ObsRoot) { $ObsRoot = Join-Path $env:ProgramFiles 'obs-studio' }
    foreach ($Entry in @(
        @{Source = (Join-Path $ObsRoot 'obs-plugins\64bit\bacons-helper.dll'); Name = 'legacy-bacons-helper.dll'},
        @{Source = (Join-Path $ObsRoot 'data\obs-plugins\bacons-helper'); Name = 'legacy-data'}
    )) {
        if (Test-Path -LiteralPath $Entry.Source) {
            New-Item -ItemType Directory -Path $Backup -Force | Out-Null
            try { Move-Item -LiteralPath $Entry.Source -Destination (Join-Path $Backup $Entry.Name) }
            catch { Write-Warning "New plugin installed, but legacy files could not be moved: $($Entry.Source). $($_.Exception.Message)" }
        }
    }
}
Write-Host "Installed $($Metadata.display_name) $($Metadata.version) to $Target"
if (Test-Path -LiteralPath $Backup) { Write-Host "Previous plugin files backed up to $Backup" }
Write-Host 'Start OBS Studio 33+ and open Tools -> Plugin Manager. Restart OBS after changing its enabled state.'
