param(
    [switch]$NoBootstrap
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Root = (Split-Path -Parent $PSScriptRoot)
Set-Location $Root

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
    $Bootstrap = Join-Path $DepsRoot 'obs-plugintemplate'
    New-Item -ItemType Directory -Force -Path $DepsRoot | Out-Null

    if (-not (Test-Path (Join-Path $Bootstrap 'CMakePresets.json'))) {
        if (Test-Path $Bootstrap) {
            Remove-Item -Recurse -Force $Bootstrap
        }
        Write-Host 'Cloning the official OBS plugin template dependency bootstrap...'
        git clone --depth 1 https://github.com/obsproject/obs-plugintemplate.git $Bootstrap
        if ($LASTEXITCODE -ne 0) { throw "OBS plugin-template clone failed with exit code $LASTEXITCODE" }
    } else {
        Write-Host "Using existing OBS plugin-template bootstrap: $Bootstrap"
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

Write-Host 'Configuring Bacons Helper OBS Companion...'
cmake --preset windows-x64 --fresh
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

$Dll = Join-Path $Dist 'obs-plugins\64bit\bacons-helper.dll'
if (-not (Test-Path $Dll)) {
    Write-Host 'Staged files:'
    Get-ChildItem -Path $Dist -Recurse -File | Select-Object FullName
    throw "Expected plugin DLL was not staged: $Dll"
}

$Locale = Join-Path $Dist 'data\obs-plugins\bacons-helper\locale\en-US.ini'
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

$TlsDist = Join-Path $Dist 'data\obs-plugins\bacons-helper\qt-plugins\tls'
New-Item -ItemType Directory -Force -Path $TlsDist | Out-Null
Copy-Item -LiteralPath $SchannelBackend -Destination (Join-Path $TlsDist 'qschannelbackend.dll') -Force

$StagedSchannel = Join-Path $TlsDist 'qschannelbackend.dll'
if (-not (Test-Path -LiteralPath $StagedSchannel)) {
    throw "Expected Schannel TLS backend was not staged: $StagedSchannel"
}
Write-Host "Staged Qt Schannel TLS backend: $StagedSchannel"

$Zip = Join-Path $DistRoot 'bacons-helper-windows-x64.zip'
if (Test-Path $Zip) { Remove-Item -Force $Zip }
Compress-Archive -Path (Join-Path $Dist '*') -DestinationPath $Zip
Write-Host "Created $Zip"
