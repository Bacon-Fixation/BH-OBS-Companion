param(
    [switch]$NoBootstrap,
    [switch]$ModernOnly
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Root = (Split-Path -Parent $PSScriptRoot)
Set-Location $Root

# Pin the template that resolves OBS 31.1.1 and its matching Qt dependencies.
$CompatibilitySdk = '31.1.1'
$TemplateRef = '3e7d7ac3b5342cd7d9b88890b9c70b472d1520fc'

Write-Host "Bacons Helper OBS Companion root: $Root"
Write-Host "PowerShell: $($PSVersionTable.PSVersion)"

$null = Get-Command cmake -ErrorAction Stop
$null = Get-Command git -ErrorAction Stop
cmake --version
git --version

function Get-CMakeCacheValue {
    param(
        [Parameter(Mandatory=$true)][string]$CachePath,
        [Parameter(Mandatory=$true)][string]$Name
    )

    $escaped = [Regex]::Escape($Name)
    $line = Get-Content -LiteralPath $CachePath | Where-Object {
        $_ -match "^${escaped}:(PATH|FILEPATH|STRING)="
    } | Select-Object -First 1

    if (-not $line) { return $null }
    return $line.Substring($line.IndexOf('=') + 1).Trim('"')
}

function Find-CMakePackageDir {
    param(
        [Parameter(Mandatory=$true)][string]$SearchRoot,
        [Parameter(Mandatory=$true)][string]$ConfigFile
    )

    $match = Get-ChildItem -LiteralPath $SearchRoot -Recurse -File -Filter $ConfigFile -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $match) { return $null }
    return $match.Directory.FullName
}

function Assert-PackageDir {
    param(
        [Parameter(Mandatory=$true)][string]$Name,
        [Parameter(Mandatory=$true)][string]$Directory,
        [Parameter(Mandatory=$true)][string]$ConfigFile
    )

    if ([string]::IsNullOrWhiteSpace($Directory)) {
        throw "$Name package directory is empty."
    }
    $config = Join-Path $Directory $ConfigFile
    if (-not (Test-Path -LiteralPath $config)) {
        throw "$Name package config was not found: $config"
    }
    Write-Host "$Name package: $Directory"
}

if (-not $NoBootstrap) {
    $DepsRoot = Join-Path $Root '.deps'
    $Bootstrap = Join-Path $DepsRoot "obs-plugintemplate-$CompatibilitySdk"
    New-Item -ItemType Directory -Force -Path $DepsRoot | Out-Null

    if (-not (Test-Path (Join-Path $Bootstrap 'CMakePresets.json'))) {
        if (Test-Path $Bootstrap) {
            Remove-Item -Recurse -Force $Bootstrap
        }
        Write-Host "Fetching the pinned OBS plugin template for SDK $CompatibilitySdk..."
        git init --quiet $Bootstrap
        if ($LASTEXITCODE -ne 0) { throw "OBS template initialization failed with exit code $LASTEXITCODE" }
        git -C $Bootstrap fetch --quiet --depth 1 https://github.com/obsproject/obs-plugintemplate.git $TemplateRef
        if ($LASTEXITCODE -ne 0) { throw "OBS template fetch failed with exit code $LASTEXITCODE" }
        git -C $Bootstrap checkout --quiet --detach FETCH_HEAD
        if ($LASTEXITCODE -ne 0) { throw "OBS template checkout failed with exit code $LASTEXITCODE" }
    } else {
        Write-Host "Using existing OBS plugin-template bootstrap: $Bootstrap"
    }

    $ActualTemplateRef = git -C $Bootstrap rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $ActualTemplateRef -ne $TemplateRef) {
        throw "The cached OBS template is not at the expected commit. Move $Bootstrap aside and rerun the script."
    }

    Write-Host 'Preparing OBS development packages and OBS-compatible Qt 6...'
    Push-Location $Bootstrap
    try {
        cmake --preset windows-x64 --fresh
        if ($LASTEXITCODE -ne 0) { throw "OBS dependency bootstrap configure failed with exit code $LASTEXITCODE" }
    }
    finally {
        Pop-Location
    }

    $CachePath = Join-Path $Bootstrap 'build_x64\CMakeCache.txt'
    if (-not (Test-Path $CachePath)) {
        throw "OBS dependency bootstrap did not create $CachePath"
    }

    # The OBS template exposes libobs/frontend as package directories, while the
    # general CMAKE_PREFIX_PATH mainly contains obs-deps and Qt. Carry all of
    # them into this project's configure rather than relying on the prefix alone.
    $env:BH_OBS_CMAKE_PREFIX_PATH = Get-CMakeCacheValue -CachePath $CachePath -Name 'CMAKE_PREFIX_PATH'
    $env:BH_OBS_LIBOBS_DIR = Get-CMakeCacheValue -CachePath $CachePath -Name 'libobs_DIR'
    $env:BH_OBS_FRONTEND_DIR = Get-CMakeCacheValue -CachePath $CachePath -Name 'obs-frontend-api_DIR'
    $env:BH_OBS_QT6_DIR = Get-CMakeCacheValue -CachePath $CachePath -Name 'Qt6_DIR'

    # Fallback discovery covers template/cache changes between OBS releases.
    $TemplateDeps = Join-Path $Bootstrap '.deps'
    if (-not $env:BH_OBS_LIBOBS_DIR) {
        $env:BH_OBS_LIBOBS_DIR = Find-CMakePackageDir -SearchRoot $TemplateDeps -ConfigFile 'libobsConfig.cmake'
    }
    if (-not $env:BH_OBS_FRONTEND_DIR) {
        $env:BH_OBS_FRONTEND_DIR = Find-CMakePackageDir -SearchRoot $TemplateDeps -ConfigFile 'obs-frontend-apiConfig.cmake'
    }
    if (-not $env:BH_OBS_QT6_DIR) {
        $env:BH_OBS_QT6_DIR = Find-CMakePackageDir -SearchRoot $TemplateDeps -ConfigFile 'Qt6Config.cmake'
    }
}

Assert-PackageDir -Name 'libobs' -Directory $env:BH_OBS_LIBOBS_DIR -ConfigFile 'libobsConfig.cmake'
Assert-PackageDir -Name 'obs-frontend-api' -Directory $env:BH_OBS_FRONTEND_DIR -ConfigFile 'obs-frontend-apiConfig.cmake'
Assert-PackageDir -Name 'Qt6' -Directory $env:BH_OBS_QT6_DIR -ConfigFile 'Qt6Config.cmake'

if (-not $env:BH_OBS_CMAKE_PREFIX_PATH) {
    # Qt's package directory and OBS package directories are enough for explicit
    # *_DIR lookups, but retain a sane prefix for transitive OBS dependencies.
    $env:BH_OBS_CMAKE_PREFIX_PATH = "$env:BH_OBS_QT6_DIR;$env:BH_OBS_LIBOBS_DIR;$env:BH_OBS_FRONTEND_DIR"
}

Write-Host 'Resolved CMake package inputs:'
Write-Host "  libobs_DIR=$env:BH_OBS_LIBOBS_DIR"
Write-Host "  obs-frontend-api_DIR=$env:BH_OBS_FRONTEND_DIR"
Write-Host "  Qt6_DIR=$env:BH_OBS_QT6_DIR"
Write-Host "  CMAKE_PREFIX_PATH=$env:BH_OBS_CMAKE_PREFIX_PATH"

# OBS config packages can append a fourth build component to the SDK version.
$RequiredSdk = if ($ModernOnly) { '' } else { "$CompatibilitySdk...<31.1.2" }
Write-Host 'Configuring Bacons Helper OBS Companion...'
cmake --preset windows-x64 --fresh -DBH_OBS_INSTALL_LAYOUT=BUNDLE "-DBH_OBS_REQUIRED_SDK_RANGE=$RequiredSdk"
if ($LASTEXITCODE -ne 0) { throw "Bacons Helper configure failed with exit code $LASTEXITCODE" }

Write-Host 'Building Bacons Helper OBS Companion...'
cmake --build --preset windows-x64 --parallel
if ($LASTEXITCODE -ne 0) { throw "Bacons Helper build failed with exit code $LASTEXITCODE" }

$DistRoot = Join-Path $Root 'dist'
$Dist = Join-Path $DistRoot 'windows-x64'
New-Item -ItemType Directory -Force -Path $DistRoot | Out-Null
if (Test-Path $Dist) { Remove-Item -Recurse -Force $Dist }

Write-Host 'Staging OBS plugin files...'
cmake --install build_x64 --config RelWithDebInfo --prefix $Dist
if ($LASTEXITCODE -ne 0) { throw "Bacons Helper install/staging failed with exit code $LASTEXITCODE" }

$Dll = Join-Path $Dist 'bacons-helper\bacons-helper.dll'
if (-not (Test-Path $Dll)) {
    Write-Host 'Staged files:'
    Get-ChildItem -Path $Dist -Recurse -File | Select-Object FullName
    throw "Expected plugin DLL was not staged: $Dll"
}

$Detector = Join-Path $Dist 'bacons-helper\data\language-guesser\model.json'
if (-not (Test-Path $Detector)) { throw "Local language detector was not staged: $Detector" }

$Locale = Join-Path $Dist 'bacons-helper\data\locale\en-US.ini'
if (-not (Test-Path $Locale)) {
    throw "Expected plugin locale file was not staged: $Locale"
}

# QNetworkAccessManager relies on a Qt TLS backend for HTTPS. OBS supplies the
# Qt runtime itself, so ship only the matching Schannel plugin from the exact Qt
# dependency tree used to build this companion. Schannel uses the Windows TLS
# stack and does not require separate OpenSSL DLLs.
$QtPrefix = (Resolve-Path (Join-Path $env:BH_OBS_QT6_DIR '..\..\..')).Path
$SchannelBackend = Join-Path $QtPrefix 'plugins\tls\qschannelbackend.dll'
if (-not (Test-Path -LiteralPath $SchannelBackend)) {
    $SchannelBackend = Get-ChildItem -LiteralPath $QtPrefix -Recurse -File -Filter 'qschannelbackend.dll' -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
if ([string]::IsNullOrWhiteSpace($SchannelBackend) -or -not (Test-Path -LiteralPath $SchannelBackend)) {
    throw "Qt Schannel TLS backend was not found below the resolved Qt prefix: $QtPrefix"
}

$TlsDist = Join-Path $Dist 'bacons-helper\data\qt-plugins\tls'
New-Item -ItemType Directory -Force -Path $TlsDist | Out-Null
Copy-Item -LiteralPath $SchannelBackend -Destination (Join-Path $TlsDist 'qschannelbackend.dll') -Force

$StagedSchannel = Join-Path $TlsDist 'qschannelbackend.dll'
if (-not (Test-Path -LiteralPath $StagedSchannel)) {
    throw "Expected Schannel TLS backend was not staged: $StagedSchannel"
}
Write-Host "Staged Qt Schannel TLS backend: $StagedSchannel"

$Manifest = Join-Path $Dist 'bacons-helper\data\manifest.json'
if (-not (Test-Path -LiteralPath $Manifest)) { throw "OBS plugin manager manifest was not staged: $Manifest" }
$Metadata = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
if ($Metadata.id -ne 'bacons-helper' -or $Metadata.version -ne ((Get-Content -LiteralPath 'buildspec.json' -Raw | ConvertFrom-Json).version)) { throw 'Staged manager metadata does not match this build.' }
Copy-Item -LiteralPath (Join-Path $Root 'scripts\install-windows.ps1') -Destination (Join-Path $Dist 'install-windows.ps1')
Copy-Item -LiteralPath (Join-Path $Root 'OBS33-PLUGIN-MANAGER.md') -Destination (Join-Path $Dist 'OBS33-PLUGIN-MANAGER.md')
Copy-Item -LiteralPath (Join-Path $Root 'OBS-WINDOWS-COMPATIBILITY.md') -Destination $Dist

$Zip = Join-Path $DistRoot 'bacons-helper-windows-x64.zip'
if (Test-Path $Zip) { Remove-Item -Force $Zip }
Compress-Archive -Path (Join-Path $Dist '*') -DestinationPath $Zip
Write-Host "Created $Zip"

if (-not $ModernOnly) {
    # Reuse the same DLL and Qt TLS backend. Only the loader's layout differs.
    $LegacyDist = Join-Path $DistRoot 'windows-x64-obs31.1-32'
    if (Test-Path -LiteralPath $LegacyDist) { Remove-Item -LiteralPath $LegacyDist -Recurse -Force }
    $LegacyBinaryDir = Join-Path $LegacyDist 'obs-plugins\64bit'
    $LegacyDataParent = Join-Path $LegacyDist 'data\obs-plugins'
    New-Item -ItemType Directory -Path $LegacyBinaryDir, $LegacyDataParent -Force | Out-Null
    Copy-Item -LiteralPath $Dll -Destination (Join-Path $LegacyBinaryDir 'bacons-helper.dll')
    Copy-Item -LiteralPath (Join-Path $Dist 'bacons-helper\data') -Destination (Join-Path $LegacyDataParent 'bacons-helper') -Recurse
    Copy-Item -LiteralPath (Join-Path $Root 'OBS-WINDOWS-COMPATIBILITY.md') -Destination $LegacyDist

    $LegacyZip = Join-Path $DistRoot 'bacons-helper-windows-x64-obs31.1-32.zip'
    if (Test-Path -LiteralPath $LegacyZip) { Remove-Item -LiteralPath $LegacyZip -Force }
    Compress-Archive -Path (Join-Path $LegacyDist '*') -DestinationPath $LegacyZip
    Write-Host "Created $LegacyZip"
}
