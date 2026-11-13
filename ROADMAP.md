# Roadmap

Clean-room work. macOS and Windows are the targets that matter; Linux is what CI runs. This project does not copy another product's code, artwork, presets, or samples.

Status: done, partial, skipped.

## Phase 0 — done

Device, transport, JSON project, undo, audio clips, metronome, test tone.

## Phase 1

- [done] MIDI input, recording, piano roll, arrangement, count-in, audio recording, built-in synth.
- [partial] Mixer groups, sends, routing, and inserts (EQ, compressor, delay, reverb, limiter, utility, chorus, saturator). **Add Delay** and **Make Group** are in the window. Other inserts are stored on the track and processed in the engine.
- [partial] Sampler. Zones render at the played pitch when a track's instrument is `sampler`.
- [partial] Automation. A `mixer.gainDb` lane is applied per block. Lanes round-trip in the project file.
- [done] Session clip grid. **Session** switches with **Arrangement**. Double-click an empty slot to create a clip, click to launch, the scene number launches the row, and **Arrangement** on a column returns that track to the timeline. Launch timing is immediate, next beat, or next bar. A launched clip loops and replaces arrangement clips on that track. Right-click clears a slot. MIDI slots edit in the piano roll.
- [done] Browser, save-and-collect, WAV export. The browser lists audio files in a folder. Drag a file onto an arrangement lane or a track row to place it. **Folder** changes the directory. **Collect** copies media into `Media/`. **Export WAV** bounces 8 seconds.
- [done] VST3 hosting and plugin delay compensation. **Plug-in** scans the platform VST3 folders, loads one insert on the selected track, and opens its editor (or a generic editor). Other tracks are delayed by `compensateDelay` so they line up with the plug-in's reported latency. The identifier and state are stored in the project. Offline WAV export does not run hosted plug-ins.

### Phase 1 status

You can record MIDI, edit it, record audio, and play both. You can route a track into a group, insert a delay (up to 4 seconds on the long delay lines), launch session clips, drag audio from the browser, choose an instrument, draw volume automation, and load a VST3 insert. Live MIDI notes use sample positions inside the current buffer. Known issues: export is a fixed 8 seconds and does not include hosted plug-ins; other built-in inserts besides delay and chorus are not in the toolbar; warp markers are stored and drawn but playback is still at the file's natural speed.

## Phase 2 — partial

- [partial] Warp markers. Ctrl-click an audio clip to add one. They are drawn on the clip. Playback does not time-stretch from them yet.
- [partial] Comp lanes. Alt-click an audio clip to add a one-beat segment. The segments are drawn. Playback still uses the whole file, not the take index.
- [done] Parallel effect racks (`mixRack`) use caller-provided scratch buffers and do not allocate. They are not a rack editor in the window.
- [skipped] Ableton Link. The official library is GPLv2 and is not vendored. `linkStatus()` is a stub that stays disabled until that licence is chosen on purpose.

### Phase 2 status

Warp markers and comp segments can be drawn on audio clips. Racks no longer allocate. There is no rack editor, and playback ignores warp and comp. Link is an intentional skip.

## Phase 3 — partial

- [done] The piano roll highlights the chosen major or minor key. **In key**, **Humanize**, **Legato**, and **Reverse** edit the open clip.
- [done] Reverse, legato, and a deterministic humanize.
- [done] MPE pitch ratio (`mpeFrequencyRatio`). Per-note slide is not yet a field on `MidiNote` in the piano roll.
- [done] An LFO value that advances with the block size.

### Phase 3 status

Key highlight and the four note tools are in the piano roll. MPE slide is still only a frequency-ratio function, and the LFO is not a modulator control.

## Phase 4 — partial

- [done] FM oscillator and a simple drum voice (`fmSample`, `drumSample`), beside the existing synth, sampler, chorus, and saturator.
- [done] The instrument menu plays the FM and drum voices on a MIDI track.

### Phase 4 status

Synth, sampler, FM, and drum are selectable. Chorus and saturator exist as inserts. There is no browser of extra devices beyond that menu.

## Phase 5 — partial

- [done] **Script** opens a console for `tempo` and `add audio|midi`.
- [done] **MIDI map** learns the next controller and applies it to the selected track's volume.
- [done] Monophonic audio-to-MIDI by autocorrelation.
- [skipped] Stem separation with an open-source neural model. No model weights are shipped or downloaded. This needs a model the user chooses and a place to run it.

### Phase 5 status

The script console and MIDI-learn window are in the app. Audio-to-MIDI is still a library call, not a button. Stem separation stays skipped until a model is chosen. Link stays skipped.
