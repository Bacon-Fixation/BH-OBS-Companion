# OBS Studio 33 plugin manager — Companion 0.7.3

This update packages Bacons Helper for OBS Studio 33's third-party plugin layout and supplies the plugin manager manifest. The manager can identify **Bacons Helper OBS Companion 0.7.3** by name/version. Use **Tools → Plugin Manager** to change its enabled state, then restart OBS to apply that change. Installation and updates continue to use the GitHub release packages.

The default build now targets the OBS 33 layout. Install a built release package using the steps below; the source-code ZIP itself must first be built. The trained language model, review/import/export controls, local tuning, pairing and previous release-workflow fixes are included.

## Windows installed OBS

Close OBS, extract `bacons-helper-windows-x64.zip` to a temporary folder, then run its installer from PowerShell:

```powershell
.\install-windows.ps1 -MigrateLegacy
```

For a custom OBS installation, provide its root:

```powershell
.\install-windows.ps1 -ObsRoot 'D:\Apps\obs-studio' -MigrateLegacy
```

If ProgramData permissions prevent installation, run that installation command from an elevated PowerShell window. The plugin bundle is installed to:

```text
C:\ProgramData\obs-studio\plugins\bacons-helper\bacons-helper.dll
C:\ProgramData\obs-studio\plugins\bacons-helper\data\manifest.json
C:\ProgramData\obs-studio\plugins\bacons-helper\data\locale\en-US.ini
C:\ProgramData\obs-studio\plugins\bacons-helper\data\language-guesser\model.json
C:\ProgramData\obs-studio\plugins\bacons-helper\data\qt-plugins\tls\qschannelbackend.dll
```

The bundle folder and binary both retain the `bacons-helper` module name. A previous bundle at that target is moved to `C:\ProgramData\obs-studio\plugin-backups\bacons-helper\<timestamp-id>\previous-bundle`. This also migrates older ProgramData bundles that used `bin\64bit` beneath the same plugin folder.

`-MigrateLegacy` additionally moves only `obs-plugins\64bit\bacons-helper.dll` and `data\obs-plugins\bacons-helper` beneath the supplied OBS installation into the backup directory. It leaves other plugins alone. The new bundle is installed before legacy migration; a failure to move protected legacy files is reported as a warning. OBS 33 prioritizes the new copy over the old one.

To install manually, copy only the extracted `bacons-helper` folder into `C:\ProgramData\obs-studio\plugins`. The DLL belongs directly inside that folder, beside `data`, with no `bin\64bit` nesting. The supplied TLS backend must remain in its private data location.

## Portable Windows OBS

Use the root that contains `bin\64bit\obs64.exe`:

```powershell
.\install-windows.ps1 -Portable -ObsRoot 'D:\Portable\obs-studio' -MigrateLegacy
```

The installer writes `D:\Portable\obs-studio\plugins\bacons-helper\bacons-helper.dll` with sibling `data`. OBS portable mode ignores the global ProgramData location. The helper validates the portable root and keeps backups under that root's `plugin-backups` folder. Launch OBS in portable mode as usual.

## Native Linux

Close OBS, extract `bacons-helper-linux-x86_64.tar.gz` into a temporary directory, and run:

```sh
bash ./install-linux.sh --migrate-legacy
```

The destination is `${XDG_DATA_HOME:-$HOME/.local/share}/obs-studio/plugins/bacons-helper`, containing `bacons-helper.so` and sibling `data/manifest.json`, locale and model files. A previous target bundle is backed up under the adjacent `plugin-backups/bacons-helper` directory. `--migrate-legacy` moves the old per-user `${XDG_CONFIG_HOME:-$HOME/.config}/obs-studio/plugins/bacons-helper` bundle into that backup directory.

System-installed legacy packages should be removed using the package manager that installed them. The per-user installer does not alter system package ownership or other plugins. An optional `--plugin-root DIR` changes the plugin-root destination for custom deployments.

Linux distribution packagers can choose `-DBH_OBS_INSTALL_LAYOUT=SYSTEM` and their normal CMake install prefix. The modern system paths are `${CMAKE_INSTALL_LIBDIR}/obs-modules/plugins/bacons-helper.so` and `${CMAKE_INSTALL_DATADIR}/obs/obs-modules/plugins/bacons-helper/`.

## OBS Flatpak

Update the OBS Flatpak runtime and install the separately built extension bundle:

```sh
flatpak update com.obsproject.Studio
flatpak install --user ./bacons-helper-obs-flatpak-x86_64.flatpak
```

The extension now uses the Freedesktop **26.08 SDK** and `BH_OBS_INSTALL_LAYOUT=FLATPAK`, staging the module under `lib/obs-modules/plugins` and its resources under `share/obs/obs-modules/plugins/bacons-helper`. These match OBS 33's extension merge directories. The extension ID stays `com.obsproject.Studio.Plugin.BaconsHelper` on the `stable` branch. Pairing remains session-only in Flatpak, as before.

## Settings and older OBS releases

The installers replace only the plugin bundle. OBS `plugin_config/bacons-helper` settings, pairing storage and saved local models are kept. The module name and manager ID remain `bacons-helper`, so changing the displayed name does not create a new settings namespace.

Use the modern Windows archive for OBS 33. For OBS 31.1 or 32 on Windows x64, use the separate legacy ZIP described in [Windows compatibility and installation](OBS-WINDOWS-COMPATIBILITY.md). For source builds, `-DBH_OBS_INSTALL_LAYOUT=LEGACY` stages the older layout. The Windows dependency bootstrap retains the established OBS API baseline rather than introducing a new frontend API requirement solely for metadata/packaging. macOS is outside this project's release matrix and its source install layout is unchanged.

If the manager still shows Legacy, check the installed module path in the OBS log: an old binary may still be loading, or the DLL/so may still be nested under `bin/64bit`. Ensure the new bundle was installed for the OBS instance you actually launch. After enabling the companion in the manager, restart OBS and open **Docks → Bacons Helper**.

## Validation and limits

- All six CTest suites passed, including native model/feedback tests, corpus provenance and the new packaging suite.
- CMake staging tests built a small fixture module and verified Windows modern/legacy, Linux user/system and Flatpak destination trees and generated manifests. These are layout tests, not Windows binary execution tests.
- Linux install/update/legacy migration tests verify backups, retained settings and unrelated plugins, and rejection of incomplete bundles.
- The manifest parsed successfully in an isolated harness using the unmodified `obs_module_load_metadata` function from OBS **33.0.0**, with test adapters for OBS data/memory utilities.
- The module exports passed a C++ syntax check. Existing formatter-checkout and release-publishing tests passed.
- A Windows installation/update/portable migration test is included in the Windows GitHub Actions job. Windows and Flatpak packages were not built or installed in this session, and no live OBS 33 GUI session was exercised. A complete local plugin link was unavailable because the container has OBS development headers but lacks the matching libobs runtime library. The existing platform Actions jobs remain responsible for producing deployable binaries.

## Upstream references

- [OBS legacy plugin developer guidance](https://obsproject.com/kb/legacy-plugin-developer-info)
- [OBS 33 plugin-loader changes, PR #13594](https://github.com/obsproject/obs-studio/pull/13594)
- [OBS 33.0.0 Windows loader](https://github.com/obsproject/obs-studio/blob/33.0.0/frontend/plugin-manager/PluginModuleLoader_Windows.cpp)
- [OBS 33.0.0 Linux loader](https://github.com/obsproject/obs-studio/blob/33.0.0/frontend/plugin-manager/PluginModuleLoader_Linux.cpp)
- [OBS 33.0.0 Flatpak loader](https://github.com/obsproject/obs-studio/blob/33.0.0/frontend/plugin-manager/PluginModuleLoader_Flatpak.cpp)
- [OBS metadata reader](https://github.com/obsproject/obs-studio/blob/33.0.0/libobs/obs-module.c)
- [OBS Flatpak runtime/SDK guidance](https://github.com/obsproject/obs-studio/discussions/11956)
