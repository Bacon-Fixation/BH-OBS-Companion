# Bacons Helper OBS Companion

Native OBS Studio dock for controlling supported Bacons Helper channel settings and live tools without leaving OBS.

## Features

- Securely pair OBS with a Bacons Helper channel.
- Manage supported channel toggles and Stream Events.
- Start and cancel the existing Bacons Helper countdown.
- Open the full Dashboard, OBS Overlays, Custom Mini-Game, Timed Actions, Loyalty/Giveaway, and Walk-On editors.
- Revoke individual OBS installations from the Bacons Helper Dashboard.

The plugin does **not** store Twitch OAuth tokens, Twitch refresh tokens, Bacons Helper website sessions, or bot credentials.

## Pairing

1. Sign in to Bacons Helper and open **Dashboard -> OBS Plugin**.
2. Generate a pairing code.
3. In OBS, open **Docks -> Bacons Helper**.
4. Enter the code and select **Pair**.
5. OBS receives a separate `bh_obs_...` credential scoped to that channel.

Pairing codes are single-use and expire after a short period. Individual OBS installations can be revoked from the Dashboard.

## Credential storage

- **Windows:** Windows DPAPI (`CryptProtectData`).
- **Native Linux:** Secret Service via `libsecret`.
- **Flatpak:** pairing credentials are intentionally kept in memory for the current OBS process rather than written to reversible local storage. Pair again after restarting OBS.

## Repository layout

```text
.
├─ .github/workflows/build.yml
├─ data/locale/en-US.ini
├─ flatpak/
├─ scripts/
│  ├─ package-windows.ps1
│  ├─ package-linux.sh
│  └─ package-flatpak.sh
├─ src/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ CHANGELOG.md
└─ README.md
```

## Windows x64 build

Requirements:

- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.28+
- Git

Build and package:

```powershell
.\scripts\package-windows.ps1
```

If `BH_OBS_CMAKE_PREFIX_PATH` is not set, the packaging script uses the official OBS plugin-template bootstrap to prepare `libobs`, `obs-frontend-api`, and an OBS-compatible Qt 6 development environment.

If you already have an OBS/Qt development environment:

```powershell
$env:BH_OBS_CMAKE_PREFIX_PATH = 'C:\path\to\obs-sdk;C:\path\to\qt6'
.\scripts\package-windows.ps1 -NoBootstrap
```

Output:

```text
dist/bacons-helper-windows-x64.zip
```

The packaged OBS layout contains:

```text
obs-plugins/64bit/bacons-helper.dll
data/obs-plugins/bacons-helper/locale/en-US.ini
```

Copy those directories into the OBS Studio installation directory.

## Native Linux x86_64 build

Ubuntu/Debian dependencies:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config \
  qt6-base-dev libobs-dev libsecret-1-dev
```

Build and package:

```bash
chmod +x scripts/package-linux.sh
./scripts/package-linux.sh
```

Output:

```text
dist/bacons-helper-linux-x86_64.tar.gz
```

## OBS Flatpak build

Build the OBS extension package with:

```bash
chmod +x scripts/package-flatpak.sh
./scripts/package-flatpak.sh
```

Output:

```text
dist/bacons-helper-obs-flatpak-x86_64.flatpak
```

See `flatpak/README.md` for Flatpak-specific details.

## GitHub Actions

The repository-root workflow automatically builds:

- Windows x64
- Native Linux x86_64
- OBS Flatpak x86_64

Every successful workflow run uploads platform packages as Actions artifacts. Push a version tag such as:

```bash
git tag v0.2.4
git push origin v0.2.4
```

to create a GitHub Release containing all three packages.

## Troubleshooting

If the dock does not appear, restart OBS and check **Help -> Log Files -> View Current Log**. Search for `bacons-helper`; OBS normally reports why a plugin module failed to load.

If pairing fails, confirm `https://baconshelper.com` is reachable and generate a new pairing code if the previous code expired or was already used.

## Project

Bacons Helper  
https://baconshelper.com

## Windows CMake dependency note

On Windows, do **not** run `cmake --preset windows-x64` first on a fresh clone. The normal OBS installation does not include the development CMake packages. Run:

```powershell
.\scripts\package-windows.ps1
```

The script uses the official OBS plugin-template bootstrap, resolves `libobs_DIR`, `obs-frontend-api_DIR`, and `Qt6_DIR`, configures the project, builds it, and creates `dist\bacons-helper-windows-x64.zip`. After the first successful bootstrap, advanced users may reuse the resolved package directories for manual CMake builds.
