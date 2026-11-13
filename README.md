# Waveform Studio

Phase 0 of an original desktop DAW. The working name is Waveform Studio. This is a clean-room project: it does not use another workstation's code, artwork, presets, or samples.

What this slice does:

- Opens the default audio device and renders a real-time-safe callback.
- Transport with play, pause, stop, tempo, time signature, a sample playhead, and a loop.
- A project of audio and MIDI tracks, clips, and a per-track mixer (gain, pan, mute, solo).
- JSON save and load, plus undo and redo.
- Plays a WAV (also AIFF, FLAC, or Ogg) in time with the transport through a stereo master.
- A small window: transport, tempo, device rate and buffer, track list, and an add-file button.
- A metronome locked to the clock, and a test tone that sounds even when the transport is stopped.

MIDI clips are stored and round-trip through the project file. They do not make sound yet. There is no recording, no time-stretch, and no plug-in hosting.

The engine and the thread model are described in [ARCHITECTURE.md](ARCHITECTURE.md). The project file is specified in [docs/PROJECT_FORMAT.md](docs/PROJECT_FORMAT.md).

## Requirements

- CMake 3.22 or newer
- A C++20 compiler
- Git (the first configure clones JUCE 9.0.5, Catch2, and nlohmann/json)
- macOS 12 or newer, Windows 10 or newer, or a recent Linux desktop

The first configure downloads JUCE and can take a few minutes. Later configures reuse the download.

ASIO is **off** unless you pass `-DWAVEFORM_ENABLE_ASIO=ON`. A normal Windows build uses WASAPI and does not need the ASIO SDK.

## macOS

Install Xcode, including the command-line tools, then CMake and Ninja:

```bash
xcode-select --install
brew install cmake ninja
```

CoreAudio is part of the SDK. There is no extra audio library to install.

### Ninja

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
open build/bin/WaveformStudio.app
```

`cmake --preset macos-ninja` is the same configure step.

### Xcode

```bash
cmake -S . -B build-xcode -G Xcode
cmake --build build-xcode --config Release
ctest --test-dir build-xcode -C Release --output-on-failure
open build-xcode/bin/Release/WaveformStudio.app
```

Or open `build-xcode/WaveformStudio.xcodeproj` and run the `WaveformStudio` scheme.

### Universal binary (Apple silicon and Intel)

```bash
cmake -S . -B build-universal -G Ninja -DCMAKE_BUILD_TYPE=Release -DWAVEFORM_MACOS_UNIVERSAL=ON
cmake --build build-universal
lipo -info build-universal/bin/WaveformStudio.app/Contents/MacOS/WaveformStudio
```

The deployment target is macOS 12. Running the Intel slice on an Apple silicon Mac needs Rosetta. `cmake --preset macos-universal` configures this build.

## Windows

Install Visual Studio 2022 with the **Desktop development with C++** workload. That includes CMake and the Windows SDK that WASAPI needs.

Use a Developer PowerShell, or any terminal where `cmake` can see the VS 2022 generator.

### Visual Studio 2022

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DWAVEFORM_ENABLE_ASIO=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\bin\Release\WaveformStudio.exe
```

`cmake --preset windows-vs2022` configures the same way. ASIO stays off, so the Audio Device window lists WASAPI devices (DirectSound remains available as a fallback).

### Ninja, from a Visual Studio environment

```bat
cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DWAVEFORM_ENABLE_ASIO=OFF
cmake --build build-ninja
build-ninja\bin\WaveformStudio.exe
```

### ASIO (optional)

Leave this off unless you have a reason to use an ASIO interface. Turning it on compiles ASIO support and the binary is subject to Steinberg's ASIO SDK licence. JUCE's bundled headers are used unless you point at your own SDK:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DWAVEFORM_ENABLE_ASIO=ON
```

Or, with a downloaded SDK:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
  -DWAVEFORM_ENABLE_ASIO=ON -DWAVEFORM_ASIO_SDK_DIR=C:\path\to\asiosdk
```

`WAVEFORM_ASIO_SDK_DIR` is the directory that contains `common\iasiodrv.h`. This repository does not ship the SDK, and CI builds with ASIO disabled.

## Linux

Linux is a secondary target, used for tests and the offline renderer.

```bash
sudo apt-get install ninja-build g++ cmake pkg-config \
  libasound2-dev libfreetype6-dev libfontconfig1-dev \
  libx11-dev libxext-dev libxrandr-dev \
  libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev libglu1-mesa-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/bin/WaveformStudio
```

The device backend is ALSA.

## Try it

1. Launch Waveform Studio. The status line names the device, sample rate, and buffer size.
2. Turn on **Test Tone**. You should hear 440 Hz. Open **Audio Device** if you do not.
3. Press **Load Demo**. It loads `examples/demo.json` and an original four-second tone, `examples/demo-tone.wav`.
4. Press **Play** (or Space). The tone starts on beat 1 and the counter advances as `bar.beat.tick`. **Click** adds a metronome on the same grid. **Loop** repeats bars 1–2 of the demo.
5. **Add Audio File** places a WAV, AIFF, FLAC, or Ogg clip at the playhead on the selected audio track.
6. **Save** writes a JSON project. Paths inside the project folder are stored relative to that file.
7. Undo and redo cover tempo, tracks, clips, and mixer moves. Ctrl+Z / Cmd+Z and Ctrl+Shift+Z / Cmd+Shift+Z are wired.

Change the sample rate or buffer from the combo boxes or the device window. Clips are resampled to the new rate on the message thread before the callback sees them.

Without speakers, the same mix can be written to a file:

```bash
./build/bin/waveform-render \
  --project examples/demo.json \
  --output /tmp/demo.wav \
  --seconds 2 \
  --sample-rate 48000
```

On Windows the tool is `build\bin\Release\waveform-render.exe`. It prints the peak and returns 0 when the file is audible. `--sample-rate 44100` also resamples the 48 kHz demo. `--metronome` and `--test-tone` mix those sources in.

## Tests

Catch2 covers the timing math, transport (including loop splits and tempo changes that keep the musical position), the mixer, metronome and test-tone phase, the lock-free queue, JSON round-trip, undo, and cross-platform relative paths.

`ctest` also runs `waveform-render` on the demo at 48 kHz and 44.1 kHz.

## Layout

```
src/core/      clock, document, commands, JSON, mixer  (no JUCE)
src/engine/    device callback, file loading, offline render
src/app/       the Phase 0 window
src/render/    waveform-render
tests/         Catch2
examples/      demo project and generated tone
```

## Limits

- Clips are decoded into memory, capped at 10 minutes.
- Audio plays at its recorded speed. Tempo does not stretch it.
- MIDI notes are saved, not synthesised. Soloing only a MIDI track silences the audio tracks.
- One stereo master. The output is hard-clipped at full scale.
- Resampling is linear, done when a file is loaded or the device rate changes.
- Moving a fader rebuilds the mix graph on the message thread.
- Stop returns to the start. Pause holds the playhead.
- The product name is a placeholder.

## Phase 1

A useful next slice is an arrangement view on top of this clock, plus sound for MIDI:

- One lane per track, clip rectangles, a playhead, and click-to-seek via the existing transport command.
- Peak data built when a file is decoded.
- Record the device input into a new audio clip.
- A small synth voice so stored MIDI clips play, and a piano roll that edits `MidiNote` through the command stack.
- Keep plug-in hosting behind a `TrackProcessor` prepared on the message thread and processed after the clip sum. VST3 and Audio Units can implement that later without changing the document types until a file-version bump.
- Then sends, a session grid that launches clips by id, and warp as an explicit mode that does not change the meaning of `lengthSeconds`.

Details are in [ARCHITECTURE.md](ARCHITECTURE.md).

## Licence

The source in this repository is MIT. See [LICENSE](LICENSE). Binaries link JUCE, which has its own licence. ASIO, if you enable it, adds the ASIO SDK terms.
