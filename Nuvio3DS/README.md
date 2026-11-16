# Nuvio3DS — Native New Nintendo 3DS XL Client

A ground-up **C++17** native homebrew client for the **New Nintendo 3DS XL** (`devkitARM` + `libctru` + `citro2d` + `3ds-curl` + `mbedTLS` + `MVDSTD`), designed to speak the **Stremio / Nuvio Addon Protocol**.

## Hardware Targets & Design Notes

- **Target Hardware:** New Nintendo 3DS / New Nintendo 3DS XL (requires New 3DS `MVDSTD` hardware H.264 decoder + `osSetSpeedupEnable(true)` 804 MHz + L2 cache).
- **Networking & TLS:** Uses `3ds-curl` + `mbedTLS` over `socInit` rather than the legacy `http:C` system service so modern TLS 1.2/1.3 endpoints (TMDB, Stremio/Nuvio addons, CDNs) connect cleanly.
- **Video Decoding:** Uses the hardware `mvd:STD` service (`MVDMODE_VIDEOPROCESSING`, `MVD_INPUT_H264`, `MVD_OUTPUT_RGB565`) to decode H.264 Annex-B NAL streams directly into linear memory and upload to a `C3D_Tex` for the 400×240 top screen.
- **Stream Compatibility:** The New 3DS XL Wi-Fi chip is 2.4 GHz 802.11b/g (~200–600 KB/s real-world) and `MVDSTD` caps reliably around **H.264 480p** (240p–360p ideal). Nuvio3DS automatically scores streams from your addons (`MVD Direct`, `Transcode Recommended`, `Unsupported Torrent/HEVC`).

## Prerequisites

### Windows 11 (Double-Click `build-windows.bat` or GitHub Actions — No `make` Needed)

You do **not** need `make` or Linux commands on Windows 11:

- **Option A — Local Windows Build (`build-windows.bat`):**
  1. Download and run the **devkitPro Windows Graphical Installer** (`devkitProUpdater`) from [devkitpro.org](https://devkitpro.org) and select **3DS Development** (installs to `C:\devkitPro`).
  2. *(Optional, for `.cia` / FBI QR installs)* Download the Windows zip of **Project_CTR `makerom`** and drop `makerom.exe` into `C:\devkitPro\tools\bin\` (or right beside `build-windows.bat`).
  3. Extract `Nuvio3DS-native-client.zip` in File Explorer and double-click **`build-windows.bat`** (or run `.\build-windows.bat` in PowerShell / Command Prompt). It compiles `Nuvio3DS.3dsx` (and `Nuvio3DS.cia` if `makerom.exe` is present).
- **Option B — Zero-Install Cloud Build (GitHub Actions):**
  Upload the extracted folder to a GitHub repository. The included `.github\workflows\build.yml` compiles `Nuvio3DS.3dsx` and `Nuvio3DS.cia` automatically in GitHub Actions so you can download the binaries directly.

### Arch Linux (`pacman` + AUR)

Arch Linux integrates devkitPro directly into system `pacman`:

1. Import and sign the devkitPro GPG key, then install the keyring:
   ```bash
   sudo pacman-key --recv BC26F752D25B92CE272E0F44F7FD5492264BB9D0 --keyserver keyserver.ubuntu.com
   sudo pacman-key --lsign BC26F752D25B92CE272E0F44F7FD5492264BB9D0
   sudo pacman -U https://pkg.devkitpro.org/devkitpro-keyring.pkg.tar.zst
   ```
2. Add devkitPro repositories to `/etc/pacman.conf`:
   ```ini
   [dkp-libs]
   Server = https://pkg.devkitpro.org/packages

   [dkp-linux]
   Server = https://pkg.devkitpro.org/packages
   ```
3. Sync and install the toolchain + 3DS libraries, then load environment variables:
   ```bash
   sudo pacman -Syu base-devel unzip python 3ds-dev 3ds-curl 3ds-mbedtls 3ds-zlib
   source /etc/profile.d/devkit-env.sh
   ```
4. *(For `.cia` / FBI QR builds)* Install `makerom` from the AUR (`yay -S makerom` / `paru -S makerom`) or copy the Linux `makerom` binary to `/usr/local/bin/makerom`.

## Build Instructions

```text
make        # builds Nuvio3DS.3dsx + Nuvio3DS.smdh
make cia    # builds ELF + Nuvio3DS.cia (requires makerom in PATH)
```

## Deploying to Your New 3DS XL SD Card

Copy **both** `Nuvio3DS.3dsx` and `nuvio3ds.xml` into the same folder on your SD card so the Homebrew Launcher grants `mvd:STD` hardware decoder permissions:

```text
sdmc:/3ds/Nuvio3DS/Nuvio3DS.3dsx
sdmc:/3ds/Nuvio3DS/nuvio3ds.xml
```

Saved addons, account session tokens, and synced settings are persisted automatically to:

```text
sdmc:/3ds/Nuvio3DS/addons.json
sdmc:/3ds/Nuvio3DS/account.json
sdmc:/3ds/Nuvio3DS/settings.json
```

## Installing via FBI QR Code

After compiling `Nuvio3DS.cia` (`make cia`), host it over your local Wi-Fi and scan the QR code in **FBI → Remote Install → Scan QR Code**:

- **Windows 11 (PowerShell)** (allow Private Network firewall access if prompted):
  ```powershell
  py -m http.server 8080
  ipconfig   # use your IPv4 Address in the studio QR generator
  ```
- **Arch Linux**:
  ```bash
  python -m http.server 8080
  ip -br a   # use your LAN IP; allow TCP 8080 if ufw/firewalld is enabled
  ```

## Nuvio Account Sync, Settings & Searching

- **Searching:** Press `X` anywhere in the Catalog or Streams view to open the native 3DS Software Keyboard (`SwkbdState`) and query enabled catalog addons (`/catalog/{type}/{id}/search={query}.json`).
- **Account Sync & Settings:** Open the **Account** tab to sign in with your Nuvio / self-hosted Supabase credentials. Watch history, progress, and your **Settings** preferences (`UI Accent Color`, `Default Catalog`, `Stream Quality`, `Prefer MVD Direct`, `Auto-Play Next`, `Subtitles`, and `Wi-Fi Buffer`) sync via Supabase (`library_items` + `user_settings`) and persist to `settings.json`. Third-party keys (Trakt, TMDB, MDBList, and debrid tokens) stay local in `addons.json`.

## Controls

| Input | Action |
|---|---|
| D-Pad / Circle Pad | Navigate catalog items, streams, addons, and settings |
| A / Touch | Open title, play stream, toggle addon, or change setting |
| B | Go back / stop playback |
| X | Open Software Keyboard (`SwkbdState`) to search |
| Y | Cycle Catalog / Addons / Account / Settings tabs |
| L / R | Switch Movie / Series catalogs, adjust settings, or seek -10s / +10s |
| START | Exit to Homebrew Launcher / HOME Menu |
