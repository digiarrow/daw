# Quick start

Waveform Studio is a desktop app for recording and arranging music. This page is the short path from a fresh download to an exported WAV.

## Install

**Mac.** Download `WaveformStudio-macos` from the latest GitHub Actions run (the “macOS disk image” job). Open the disk image and drag **Waveform Studio** to Applications.

The build is ad-hoc signed, not notarized. The first time you open it, macOS may say it cannot verify the developer. Right-click the app, choose Open, then Open again. To remove that warning for everyone else, the app has to be notarized with an Apple Developer ID. The steps are in [PACKAGING.md](PACKAGING.md).

**Windows.** Download `WaveformStudio-windows` from the latest GitHub Actions run (the “Windows zip” job). Unzip it and run `WaveformStudio.exe`. Audio uses WASAPI.

**From source.**

```sh
# macOS
./scripts/package-macos.sh

# Windows, from PowerShell
./scripts/package-windows.ps1
```

## Set the audio device

1. Click **Audio Device**.
2. Pick the output you want to hear, and the input you want to record.
3. Choose a sample rate and a buffer size in the main window. A larger buffer is safer. A smaller buffer feels tighter when you play a keyboard.

The readout next to the clock shows CPU use and the latency of the current buffer, including any plug-in delay.

## Make a beat

1. Click **Add MIDI Track**.
2. Choose **Drum** in the instrument menu.
3. Double-click the MIDI lane in the arrangement to make a clip.
4. Draw notes in the piano roll. The **Slide** slider bends the selected note.
5. Press Space to hear it. Press Space again to stop.
6. Turn on **Click** if you want a metronome.
7. Drag the loop brace in the ruler, then turn on **Loop**, to cycle a section.

To use a synth or an FM tone instead, change the instrument menu. To play a sample, drop an audio file from the browser onto the MIDI track.

**Add Return** creates a return bus. On another track, pick that return in the send menu and raise the send level. **Pre** sends the signal before the fader. Leave **Pre** off and the send follows the fader.

**Mixer** shows every track as a strip with a meter.

## Record

1. Click **R** on the track you want to record. That arms it.
2. Set the count-in to 1 bar if you want a lead-in.
3. Press **R** or click **Record** and play.
4. The take lands on the track when you stop.

For audio, arm an audio track and record from the device input. For MIDI, arm a MIDI track, choose your keyboard under the MIDI input menu, and play.

## Export

Save the project first (**Cmd+S** or **Ctrl+S**).

Click **Export WAV**.

- **Whole song** or **Loop** chooses the range.
- Sample rate can be 44100, 48000, or 96000.
- Bit depth can be 16-bit, 24-bit, or 32-bit float.
- **Separate stem for each track** writes one WAV per track, next to the mix, with the track name in the file.

Loaded VST3 plug-ins are included in the bounce. **Plug-in** scans your VST3 folder, and you can stack up to four inserts. **Open editor** shows the plug-in’s own window.

The mix is written next to the project file.

## A few more keys

| Key | Action |
| --- | --- |
| Space | Play or pause |
| R | Record |
| Cmd/Ctrl+S | Save |
| Cmd/Ctrl+O | Open |
| Cmd/Ctrl+E | Export |
| Cmd/Ctrl+Z | Undo |

**Recent** reopens a project you already saved. The app also writes an autosave every 30 seconds.
