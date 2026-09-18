# Flatpak packaging

Bacons Helper is packaged as an OBS Studio Flatpak extension:

- Extension ID: `com.obsproject.Studio.Plugin.BaconsHelper`
- OBS runtime: `com.obsproject.Studio//stable`
- SDK: `org.freedesktop.Sdk//25.08`
- Extension prefix: `/app/plugins/BaconsHelper`

Build locally from the repository root:

```bash
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user -y flathub com.obsproject.Studio//stable org.freedesktop.Sdk//25.08
flatpak-builder --user --force-clean --repo=flatpak-repo flatpak-build flatpak/com.obsproject.Studio.Plugin.BaconsHelper.yml
flatpak build-bundle flatpak-repo Bacons-Helper-OBS.flatpak com.obsproject.Studio.Plugin.BaconsHelper stable
```

## Credential storage in Flatpak

The native Linux build stores the OBS-only credential with Secret Service (`libsecret`). The packaged Flatpak extension deliberately builds with `BH_LINUX_SECRET_SERVICE=OFF`, because the OBS parent sandbox does not guarantee that `libsecret` or direct Secret Service access is available to extensions.

For Flatpak, Bacons Helper therefore keeps the pairing credential only in memory for the current OBS process. Closing OBS requires pairing again on the next launch. This is less convenient, but it avoids silently writing a reversible credential to disk and keeps the Flatpak package independent of host keyring permissions.

The pairing credential remains channel-scoped and independently revocable from the Bacons Helper dashboard. A future Flatpak build can add persistent storage through a sandbox-appropriate secret mechanism without changing the server-side pairing model.
