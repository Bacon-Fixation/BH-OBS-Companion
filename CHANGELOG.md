# Changelog

## 0.2.3 - 2026-09-18

- Repackaged the OBS Companion as a standalone GitHub repository.
- Moved the build workflow to the repository-root `.github/workflows/build.yml` location GitHub Actions discovers.
- Removed UPM/subdirectory-specific workflow paths and compatibility scaffolding.
- Reduced default GitHub Actions permissions to read-only; release publishing receives `contents: write` only on tagged builds.
- Added repository-layout checks before the Windows build starts.
- Kept Windows x64, native Linux x86_64, and OBS Flatpak x86_64 packaging in one workflow.

## 0.2.2 - 2026-09-18

- Fixed GitHub Actions integration when `BH-OBS-Companion` is a subdirectory of a larger repository.
- Added explicit Windows dependency-bootstrap diagnostics and validation for libobs, obs-frontend-api, and Qt 6.
- Forced fresh CMake configuration in CI to avoid stale prefix/cache failures.
- Validated staged DLL/SO and locale files before creating build artifacts.
- Added the explicit QtNumeric include required by modern Qt for `qRound64`.

## 0.2.1 - 2026-09-18

- Added GitHub Actions path handling for alternate repository layouts.
- Added explicit artifact-path validation.

## 0.2.0 - 2026-09-18

- Added native Linux x86_64 build and packaging support.
- Added Secret Service/libsecret credential persistence for native Linux.
- Removed the early reversible `scoped:` Base64 credential fallback; legacy values are migrated into Secret Service when possible or removed from disk.
- Added a safe session-only credential mode for Flatpak builds.
- Added an OBS Flatpak extension manifest and local packaging script.
- Added Windows, native Linux, and Flatpak GitHub Actions build jobs plus tagged release assets.
- Corrected Windows CMake installation of the OBS `MODULE` library to use the `LIBRARY` artifact destination.
- Corrected Windows staged data layout to `data/obs-plugins/bacons-helper`.
- Locked the configured server field while paired so a bearer credential cannot be redirected to another HTTPS host.
- Added persistent warnings when secure credential storage is unavailable.
