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
- [partial] Session launch. Quantize-to-beat and quantize-to-bar are tested. The clip-grid window is not built.
- [partial] Collect and WAV export. **Collect** fills `Media/`. **Export WAV** bounces 8 seconds. There is no browser pane.
- [partial] Plugin delay compensation. `compensateDelay` is the delay line. A VST3 plugin is not loaded in the audio callback yet.
- [skipped for this increment] In-window VST3 browser. JUCE can host VST3; wiring a scanner into the callback is the remaining step, and it was not finished here.

### Phase 1 status

You can record MIDI, edit it, record audio, and play both. You can route a track into a group and insert a delay. Known issues: delay time is capped near 170 ms; live MIDI is one buffer late; export is a fixed 8 seconds from the saved file; the session grid and VST3 slot are not in the window.

## Phase 2 — partial

- [done] Warp marker interpolation (`warpedSourceSeconds`). Playback still uses the unwarped sample path unless a later change reads markers inside the clip renderer.
- [done] Take comp selection (`compTakeAt`).
- [done] Parallel effect racks (`mixRack`). It allocates, so it is for prepare/offline, not the audio callback.
- [skipped] Ableton Link. The official library is GPLv2 and is not vendored. `linkStatus()` is a stub that stays disabled until that licence is chosen on purpose.

### Phase 2 status

The timing helpers are tested. The arrangement does not yet show warp markers, comp lanes, or racks. Link is an intentional skip.

## Phase 3 — partial

- [done] Major and minor key tests, and snapping a note into the key.
- [done] Reverse, legato, and a deterministic humanize.
- [done] MPE pitch ratio (`mpeFrequencyRatio`). Per-note slide is not yet a field on `MidiNote` in the piano roll.
- [done] An LFO value that advances with the block size.

### Phase 3 status

Generators and key logic are library functions with tests. They are not buttons in the piano roll yet.

## Phase 4 — partial

- [done] FM oscillator and a simple drum voice (`fmSample`, `drumSample`), beside the existing synth, sampler, chorus, and saturator.
- [ ] A window to play the FM and drum voices as track instruments.

### Phase 4 status

The voices exist and are tested. They are not selectable in the UI.

## Phase 5 — partial

- [done] A small project script: `tempo` and `add audio|midi`.
- [done] MIDI CC 7 mapped to track gain.
- [done] Monophonic audio-to-MIDI by autocorrelation.
- [skipped] Stem separation with an open-source neural model. No model weights are shipped or downloaded. This needs a model the user chooses and a place to run it.

### Phase 5 status

Scripting, one control mapping, and monophonic audio-to-MIDI are tested library calls. There is no script console, no control-surface window, and no stem splitter.
