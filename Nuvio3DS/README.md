# Nuvio3DS — Native New Nintendo 3DS XL Client

A ground-up **C++17** native homebrew client for the **New Nintendo 3DS XL** (`devkitARM` + `libctru` + `citro2d` + `3ds-curl` + `mbedTLS` + `MVDSTD`), designed to speak the **Stremio / Nuvio Addon Protocol**.

## Hardware Targets & Design Notes

- **Target Hardware:** New Nintendo 3DS / New Nintendo 3DS XL (requires New 3DS `MVDSTD` hardware H.264 decoder + `osSetSpeedupEnable(true)` 804 MHz + L2 cache).
- **Networking & TLS:** Uses `3ds-curl` + `mbedTLS` over `socInit` rather than the legacy `http:C` system service so modern TLS 1.2/1.3 endpoints (TMDB, Stremio/Nuvio addons, CDNs) connect cleanly.
- **Video Decoding:** Uses the hardware `mvd:STD` service (`MVDMODE_VIDEOPROCESSING`, `MVD_INPUT_H264`, `MVD_OUTPUT_RGB565`) to decode H.264 Annex-B NAL streams directly into linear memory and upload to a `C3D_Tex` for the 400×240 top screen.
- **Stream Compatibility:** The New 3DS XL Wi-Fi chip is 2.4 GHz 802.11b/g (~200–600 KB/s real-world) and `MVDSTD` caps reliably around **H.264 480p** (240p–360p ideal). Nuvio3DS automatically scores streams from your addons (`MVD Direct`, `Transcode Recommended`, `Unsupported Torrent/HEVC`).

## Prerequisites (devkitPro)

Install `devkitPro` and the required 3DS libraries via `dkp-pacman`:

```bash
dkp-pacman -S 3ds-dev 3ds-curl 3ds-mbedtls 3ds-zlib
```

## Build Instructions

```bash
cd 3ds-client
make
```

This produces `Nuvio3DS.3dsx` and `Nuvio3DS.smdh`.

To build an installable `.cia` (requires `makerom` on `PATH`):

```bash
make cia
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

After compiling `Nuvio3DS.cia` locally (`make cia`), you can host it over your LAN and scan a QR code in **FBI → Remote Install → Scan QR Code**:

```bash
make cia
python3 -m http.server 8080
# Point the QR code in the web studio to http://<YOUR-PC-IP>:8080/Nuvio3DS.cia
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
