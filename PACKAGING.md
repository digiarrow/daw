# Packaging

Waveform Studio installs as a macOS app bundle or a Windows executable. The bundles are ad-hoc signed on macOS. They are not notarized.

## macOS

```sh
./scripts/package-macos.sh
```

That produces:

- `build-package/bin/WaveformStudio.app`
- `build-package/package/WaveformStudio-<version>-Darwin.dmg`

The script signs the app with `codesign --sign -`, which is an ad-hoc signature. Gatekeeper will still warn on another Mac until the app is notarized.

To make a universal build (Apple silicon and Intel):

```sh
WAVEFORM_MACOS_UNIVERSAL=1 ./scripts/package-macos.sh
```

### Notarization

Notarization needs an Apple Developer ID Application certificate and an app-specific password or App Store Connect API key. This repository does not store those.

1. Sign with the Developer ID instead of the ad-hoc identity: `codesign --force --deep --options runtime --sign "Developer ID Application: Your Name (TEAMID)" WaveformStudio.app`
2. Zip the app and submit it: `xcrun notarytool submit WaveformStudio.zip --apple-id you@example.com --team-id TEAMID --wait`
3. Staple the ticket: `xcrun stapler staple WaveformStudio.app`
4. Build the disk image again with `cpack`.

## Windows

From a shell that can see Visual Studio 2022:

```powershell
./scripts/package-windows.ps1
```

That produces `build-package/package/WaveformStudio-<version>-Windows.zip` containing `WaveformStudio.exe`. Unzip it and run the executable. WASAPI is the audio backend. ASIO stays off unless you point CMake at a Steinberg ASIO SDK, which has its own licence.

## Linux

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The binary is `build/bin/WaveformStudio`. There is no installer.
