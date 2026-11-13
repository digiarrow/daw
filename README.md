# Waveform Studio

Phase 0 of an original desktop DAW. The working name is Waveform Studio. This is a clean-room project: it does not use another workstation's code, artwork, presets, or samples.

What this slice does:

- Opens the default audio device and renders a real-time-safe callback.
- Transport with play, pause, stop, tempo, time signature, a sample playhead, and a loop.
- A project of audio and MIDI tracks, clips, and a per-track mixer (gain, pan, mute, solo).
- JSON save and load, plus undo and redo.
- Plays a WAV (also AIFF, FLAC, or Ogg) in time with the transport through a stereo master.
- A small window: transport, tempo, device rate and buffer, track list, and an add-file button.
- A metronome locked to the clock, with a count-in before recording, and a test tone that sounds even when the transport is stopped.
- MIDI input, MIDI recording, and playback through a built-in polyphonic synth (oscillator, ADSR, low-pass filter).
- A piano roll: draw, move, resize, delete, velocity, grid snap, and quantize. Those edits undo.
- An arrangement timeline: clips on a bar grid, drag to move, a playhead, and a loop brace.
- Audio recording from the device input onto an audio track.
- Track inserts (EQ, compressor, delay, reverb, limiter, utility, chorus, saturator), group buses, and two sends. **Add Delay** and **Make Group** are on the track toolbar.
- **Session** switches the timeline for a clip grid. Double-click a slot to create a clip, click to launch it on the beat, the bar, or immediately. A scene number launches the whole row.

There is still no time-stretch and no plug-in hosting. The synth is the built-in instrument, not a hosted plug-in.

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
6. The timeline under the transport shows every clip. Drag a clip to move it (it snaps to 16ths). Drag the copper marks in the ruler to set the loop. Click the ruler to seek. Double-click an empty MIDI lane to create a clip, then edit it in the piano roll below.
7. Choose a MIDI input. Arm a MIDI track with **R**, press **Record**, and play the keyboard after the count-in. Stop, and the take becomes a clip you can quantize or redraw. Arm an audio track the same way to record the device input. The app opens the default input as well as the output; pick another input from **Audio Device** if the take is silent.
8. **Save** writes a JSON project. Paths inside the project folder are stored relative to that file.
9. **Collect** copies used audio into a `Media` folder next to the project. **Export WAV** writes an 8 second bounce beside the project file.
10. Undo and redo cover tempo, tracks, clips, mixer moves, clip drags, and piano-roll edits. Ctrl+Z / Cmd+Z and Ctrl+Shift+Z / Cmd+Shift+Z are wired.

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

- Clips are decoded into memory, capped at 10 minutes. An audio take is capped at 3 minutes.
- Audio plays at its recorded speed. Tempo does not stretch it.
- The synth is one polyphonic voice per sounding note: two detuned oscillators, an ADSR, and a low-pass filter. Oscillators are not band-limited. Every MIDI track uses its own patch, but there is no external instrument.
- Live MIDI is applied at the start of the next audio block, so the monitoring latency is about one buffer.
- Soloing a track still silences the others, including the synth.
- One stereo master. The output is hard-clipped at full scale.
- Resampling is linear, done when a file is loaded or the device rate changes.
- Moving a fader rebuilds the mix graph on the message thread.
- Stop returns to the start. Pause holds the playhead.
- The product name is a placeholder.

## What to build next

- Mixer routing: track outputs, sends, and a real meter bridge. Faders should become atomics so they do not rebuild the graph.
- Plug-in hosting behind a `TrackProcessor` after the clip sum. VST3 on both desktops, and Audio Units on macOS. Store the identifier and state blob in a new project-file version.
- A session grid that launches the clips that already exist, by id, on the same transport.
- Waveform peaks in the arrangement, and a band-limited oscillator if the saw gets too harsh.
- Warp as an explicit mode that does not change the meaning of `lengthSeconds`.

Details are in [ARCHITECTURE.md](ARCHITECTURE.md).

## Licence

The source in this repository is MIT. See [LICENSE](LICENSE). Binaries link JUCE, which has its own licence. ASIO, if you enable it, adds the ASIO SDK terms.
