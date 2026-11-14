# Roadmap

Clean-room work. macOS and Windows are the targets that matter; Linux is what CI runs. This project does not copy another product's code, artwork, presets, or samples.

Status: done, partial, skipped.

## Phase 0 — done

Device, transport, JSON project, undo, audio clips, metronome, test tone.

## Phase 1

- [done] MIDI input, recording, piano roll, arrangement, count-in, audio recording, built-in synth.
- [done] Inserts, groups, returns, and the mixer. **Mixer** shows a strip per track with a meter, fader, mute, solo, and the send. **Add Return** creates a return. Post-fader sends follow the channel fader; **Pre** does not.
- [done] Sampler, FM, and drum. The instrument menu on a MIDI track chooses synth, sampler, FM, or drum. Dropping a browser file on a MIDI track adds a sampler zone rooted at C4.
- [done] Automation. V cycles the lane among mixer gain, pan, filter cutoff, and the first effect parameter. A track can store one lane per parameter. Gain is applied per sample. Clips can store an envelope. Other parameters use the same reader.
- [done] Session clip grid. **Session** switches with **Arrangement**. Double-click an empty slot to create a clip, click to launch, the scene number launches the row, and **Arrangement** on a column returns that track to the timeline. Launch timing is immediate, next beat, or next bar. Slots trigger, gate, toggle, or repeat, and can follow to another scene. While a session slot is running, that scene's tempo replaces the transport tempo, and stopping every slot restores the previous tempo. Recording with the session open commits the take into the selected slot. Right-click clears a slot. MIDI slots edit in the piano roll.
- [done] Browser, save-and-collect, WAV export. The browser lists WAV, AIFF, FLAC, Ogg, and MP3, and a click previews the file. **Export WAV** chooses the loop or the whole song, 44.1/48/96 kHz, 16-bit, 24-bit, or 32-bit float, includes hosted plug-ins, and can write one file per track. **Collect** copies media into `Media/`.
- [done] VST3 hosting and plugin delay compensation. **Plug-in** scans the platform VST3 folders, loads one insert on the selected track, and opens its editor (or a generic editor). Other tracks are delayed by `compensateDelay` so they line up with the plug-in's reported latency. The identifier and state are stored in the project. Offline WAV export does not run hosted plug-ins.

### Phase 1 status

You can record MIDI, edit it, record audio, and play both. You can route a track into a group, insert a delay (up to 4 seconds on the long delay lines), launch session clips, drag audio from the browser, choose an instrument, draw volume automation, and load a VST3 insert. Live MIDI notes use sample positions inside the current buffer. Packaging scripts produce an ad-hoc signed macOS disk image and a Windows zip. CI uploads both on every push. A musician’s path through install, recording, and export is in [QUICKSTART.md](QUICKSTART.md). Notarization still needs an Apple Developer ID, which is not stored here. Link and neural stem separation stay skipped.

## Phase 2 — partial

- [done] Warp markers time-stretch playback with [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) (MIT; Signalsmith Linear, also MIT, is vendored beside it). Ctrl-click an audio clip to add a marker. Pitch stays put while the duration follows the markers. Beats, tones, and complex choose the stretcher block size. Auto-warp estimates tempo and writes two markers. Rubber Band was not used; its GPL licence does not fit this tree.
- [done] Comp segments choose the take you hear. Shift-drop a file onto an audio clip to add a take. Alt-click paints a one-beat segment on the next take. Take 0 is the clip's own file.
- [done] **Rack** opens an editor for up to four parallel chains, each with up to four effects, and eight macros. Each macro can be mapped onto a parameter. Chains are mixed with preallocated scratch.
- [skipped] Ableton Link. The official library is GPLv2 and is not vendored. `linkStatus()` is a stub that stays disabled until that licence is chosen on purpose.

### Phase 2 status

Warp playback uses Signalsmith Stretch. Comp segments switch takes. The rack editor mixes parallel chains. Link is an intentional skip.

## Phase 3 — partial

- [done] The piano roll highlights the chosen major or minor key. **In key**, **Humanize**, **Legato**, and **Reverse** edit the open clip.
- [done] Reverse, legato, and a deterministic humanize.
- [done] MPE slide. The piano-roll **Slide** slider sets semitones on the selected note, and the synth, FM, and drum voices follow it.
- [done] **LFO** assigns a sine to the selected track's volume, with rate and depth.

### Phase 3 status

Key tools, MPE slide, and a volume LFO are in the window.

## Phase 4 — partial

- [done] FM oscillator, a simple drum voice (`fmSample`, `drumSample`), and a drum rack (pads, per-pad chains, choke groups), beside the existing synth, sampler, chorus, and saturator. The compressor can key from another track.
- [done] The instrument menu plays the FM and drum voices on a MIDI track.

### Phase 4 status

Synth, sampler, FM, and drum are selectable. Chorus and saturator exist as inserts. There is no browser of extra devices beyond that menu.

## Phase 5 — partial

- [done] **Script** opens a console for `tempo` and `add audio|midi`.
- [done] **MIDI map** learns the next controller and applies it to the selected track's volume.
- [done] **Audio to MIDI** turns the selected audio clip's pitch into a MIDI note.
- [skipped] Stem separation with an open-source neural model. No model weights are shipped or downloaded. This needs a model the user chooses and a place to run it.

### Phase 5 status

The script console, MIDI-learn window, and audio-to-MIDI button are in the app. Stem separation stays skipped until a model is chosen. Link stays skipped.
