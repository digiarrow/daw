# Roadmap

Clean-room work. Mac and Windows are the targets that matter; Linux is exercised in CI. Nothing here copies another product's code, artwork, presets, or samples.

Status values: done, partial, skipped.

## Phase 0

- [done] Device, transport, project JSON, undo, audio clips, metronome, test tone.

## Phase 1

- [done] MIDI input, recording, piano roll, arrangement timeline, count-in, audio recording.
- [done] Built-in polyphonic synth.
- [partial] Mixer groups, sends, and routing. Tracks can feed a group or the master, with two sends and insert slots. Built-in inserts: EQ, compressor, delay, reverb, limiter, utility, chorus, saturator. The window can insert a delay and make a group; the other inserts are in the project file.
- [ ] VST3 hosting and plugin delay compensation.
- [ ] Automation lanes.
- [ ] Session clip grid with quantized launch.
- [ ] Simple sampler.
- [ ] Browser, save-and-collect, WAV export button (offline render already exists).

## Phase 2

- [ ] Warping and time-stretch.
- [ ] Take comping.
- [ ] Instrument and effect racks.
- [skipped] Ableton Link. The official library is GPLv2; it is not vendored. A stub clock interface can be added when that licence is an explicit choice.

## Phase 3

- [ ] Scales and keys.
- [ ] MIDI generators and transformations.
- [ ] MPE.
- [ ] Modulators.

## Phase 4

- [ ] More original instruments and effects.

## Phase 5

- [ ] Scripting API.
- [ ] Control-surface mapping.
- [skipped until a model is supplied] Stem separation with an open-source neural model. No model weights are downloaded in this repository.
- [ ] Audio-to-MIDI.

## Phase 1 status

Groups, sends, and the insert processors are in the engine and covered by tests (utility gain, delay impulse, compressor, group summing). A selected track can gain a delay or be routed into a new group from the window. VST3, automation, the session grid, the sampler, the browser, collect, and a one-click export are still open. Known issue: delay time is capped at about 170 ms because the delay line is fixed-size and real-time safe.
