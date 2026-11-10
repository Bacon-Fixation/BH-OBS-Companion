param(
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [string]$LegacyArchive
)
$ErrorActionPreference = 'Stop'
$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
$Installer = Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\install-windows.ps1'
$Scratch = Join-Path ([IO.Path]::GetTempPath()) ('bh-obs-install-test-' + [Guid]::NewGuid().ToString('N'))
function Assert-True([bool]$Value, [string]$Message) { if (-not $Value) { throw $Message } }
try {
    $Plugins = Join-Path $Scratch 'obs-studio\plugins'
    $Old = Join-Path $Plugins 'bacons-helper\bin\64bit'
    New-Item -ItemType Directory -Path $Old -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $Old 'bacons-helper.dll') -Value 'legacy-module'
    $Settings = Join-Path $Scratch 'obs-studio\plugin_config\bacons-helper'
    New-Item -ItemType Directory -Path $Settings -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $Settings 'companion.ini') -Value 'preserve-settings'
    & $Installer -PackageRoot $PackageRoot -PluginRoot $Plugins
    Assert-True (Test-Path -LiteralPath (Join-Path $Plugins 'bacons-helper\bacons-helper.dll')) 'New bundle module missing.'
    Assert-True (Test-Path -LiteralPath (Join-Path $Plugins 'bacons-helper\data\manifest.json')) 'Manager manifest missing.'
    Assert-True ((Get-Content -LiteralPath (Join-Path $Settings 'companion.ini') -Raw).Trim() -eq 'preserve-settings') 'Configuration was changed.'
    $Backups = Join-Path $Scratch 'obs-studio\plugin-backups'
    Assert-True (@(Get-ChildItem -LiteralPath $Backups -Recurse -File -Filter 'bacons-helper.dll').Count -eq 1) 'Legacy module was not backed up.'
    & $Installer -PackageRoot $PackageRoot -PluginRoot $Plugins
    Assert-True (@(Get-ChildItem -LiteralPath $Backups -Recurse -Directory -Filter 'previous-bundle').Count -eq 2) 'An update did not preserve the previous bundle.'

    $Portable = Join-Path $Scratch 'portable-obs'
    New-Item -ItemType Directory -Path (Join-Path $Portable 'bin\64bit') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $Portable 'bin\64bit\obs64.exe') -Value 'fixture-only'
    New-Item -ItemType Directory -Path (Join-Path $Portable 'obs-plugins\64bit') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $Portable 'obs-plugins\64bit\bacons-helper.dll') -Value 'old-portable'
    Set-Content -LiteralPath (Join-Path $Portable 'obs-plugins\64bit\another-plugin.dll') -Value 'other-plugin'
    & $Installer -PackageRoot $PackageRoot -Portable -ObsRoot $Portable -MigrateLegacy
    Assert-True (Test-Path -LiteralPath (Join-Path $Portable 'plugins\bacons-helper\bacons-helper.dll')) 'Portable bundle was not installed.'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $Portable 'obs-plugins\64bit\bacons-helper.dll'))) 'Portable legacy module was not migrated.'
    Assert-True (Test-Path -LiteralPath (Join-Path $Portable 'obs-plugins\64bit\another-plugin.dll')) 'Another plugin was changed.'
    if ($LegacyArchive) {
        $LegacyStage = Join-Path $Scratch 'legacy-release'
        Expand-Archive -LiteralPath $LegacyArchive -DestinationPath $LegacyStage
        $Modern = Join-Path $PackageRoot 'bacons-helper'
        $LegacyData = Join-Path $LegacyStage 'data\obs-plugins\bacons-helper'
        $LegacyDll = Join-Path $LegacyStage 'obs-plugins\64bit\bacons-helper.dll'
        Assert-True (Test-Path -LiteralPath $LegacyDll -PathType Leaf) 'Legacy archive module missing.'
        Assert-True ((Get-FileHash -LiteralPath $LegacyDll).Hash -eq (Get-FileHash -LiteralPath (Join-Path $Modern 'bacons-helper.dll')).Hash) 'Windows packages have different plugin binaries.'
        foreach ($Relative in @('manifest.json', 'locale\en-US.ini', 'language-guesser\model.json', 'qt-plugins\tls\qschannelbackend.dll')) {
            $LegacyFile = Join-Path $LegacyData $Relative
            Assert-True (Test-Path -LiteralPath $LegacyFile -PathType Leaf) "Legacy archive data missing: $Relative"
            $ModernFile = Join-Path (Join-Path $Modern 'data') $Relative
            Assert-True ((Get-FileHash -LiteralPath $LegacyFile).Hash -eq (Get-FileHash -LiteralPath $ModernFile).Hash) "Legacy and modern data differ: $Relative"
        }
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $LegacyStage 'bacons-helper'))) 'Legacy archive contains the modern bundle layout.'
    }
    Write-Host 'Windows install/update/portable/legacy migration checks passed.'
} finally {
    if (Test-Path -LiteralPath $Scratch) { Remove-Item -LiteralPath $Scratch -Recurse -Force }
}
