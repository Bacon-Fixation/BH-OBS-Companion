# Windows x64 OBS compatibility

Use the ZIP that matches the OBS installation you launch. Both standard packages are built from the same Companion source using OBS SDK 31.1.1 and its matching Qt development bundle.

| OBS Studio | Windows ZIP |
| --- | --- |
| 31.1.x and 32.x | `bacons-helper-windows-x64-obs31.1-32.zip` |
| 33.x | `bacons-helper-windows-x64.zip` |

The Windows compatibility target starts at OBS 31.1. OBS 31.0 and earlier are outside this build target. The separate Linux and Flatpak packages have platform-specific dependencies and installation paths.

## OBS 31.1 and 32

1. Close OBS and extract the legacy ZIP to a temporary folder.
2. In that folder, copy `obs-plugins` and `data` into the OBS installation root, which contains `bin\64bit\obs64.exe`. Merge the directories and replace the existing Bacons Helper files. The same layout applies to installed and portable OBS. An installation under Program Files may require elevation.
3. If a previous Bacons Helper build was installed under `C:\ProgramData\obs-studio\plugins\bacons-helper\bin\64bit`, move that entire old `bacons-helper` folder outside the plugin directories before starting OBS. This avoids loading a second copy.
4. Start OBS and open **Docks → Bacons Helper**. Check **Help → Log Files → View Current Log** for the loaded plugin if the dock does not appear.

The ZIP contains the DLL under `obs-plugins\64bit` and the language model, locale and Schannel backend under `data\obs-plugins\bacons-helper`. Keep both directory trees together.

## OBS 33

Extract the modern ZIP into a temporary folder, then run its installer. When upgrading an existing installation, give the installer the root of the OBS instance whose old files should be backed up.

```powershell
.\install-windows.ps1 -ObsRoot 'C:\Program Files\obs-studio' -MigrateLegacy
```

For a portable copy, add `-Portable` and use that copy's root. See [OBS 33 installation and migration](OBS33-PLUGIN-MANAGER.md) for details. Both layouts use the same Companion module ID and leave its saved OBS settings and pairing data in place.

## Build both ZIPs

From the Companion source root, run `.\scripts\package-windows.ps1`. The script uses a pinned OBS plugin-template revision, builds the plugin once, and places both ZIPs under `dist`. With `-NoBootstrap`, supply `BH_OBS_LIBOBS_DIR`, `BH_OBS_FRONTEND_DIR`, and `BH_OBS_QT6_DIR` from the matching OBS 31.1.1 SDK and Qt build. For a custom SDK and a modern-layout archive only, use `-NoBootstrap -ModernOnly`.

- [OBS Windows plugin locations](https://obsproject.com/kb/legacy-plugin-developer-info)
