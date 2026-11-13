# Architecture

Waveform Studio is an original desktop DAW. Phase 0 is the clock, the document, and a stereo mix. Later phases plug into the types below. Nothing here is derived from another product's source, assets, or interface artwork.

## Threads

```
message thread                         audio thread (one callback)
------------------------------         ---------------------------------
UI, file dialogs, JSON                 AudioIODeviceCallback
ClipLibrary  (decode, resample)        drain SpscQueue<EngineCommand>
build RenderGraph                      Transport::advance  (int64 samples)
History / edit commands                renderLinearBlock
publish graph pointer        ------->  read graph samples (no alloc, no locks)
read atomics (position, peaks) <------ write atomics at end of the block
```

JUCE calls `audioDeviceAboutToStart` and `audioDeviceStopped` when the callback is not running. Those may allocate. The callback itself must not allocate, lock, log, read files, or touch `std::string`.

The message thread is the only producer on the command queue. The audio thread is the only consumer, and the only thread that mutates `Transport` while a device is open. If no device is open, commands are applied on the message thread so the queue cannot fill up while the app is silent.

### Graphs

`RenderGraph` is immutable after publication. Clip audio is a `shared_ptr` owned by the graph, so the audio thread never copies the pointer and never touches the reference count. It only reads the float vectors.

Publication swaps an atomic pointer. The audio thread sets `inUse` to a sentinel before it loads the pointer and clears it when the callback returns. The message thread deletes a retired graph only when it is neither current nor the pointer the callback says it is using, and not while the callback is between those two stores. `maintenance()` runs this from the UI timer. This assumes a single callback thread, which is what JUCE provides for one device.

Mixer moves rebuild the graph on the message thread. That is simple and race-free. A later phase can move gain, pan, mute, and solo onto atomics so a fader does not allocate.

## Clock

The playhead is an `int64` sample index at the device sample rate. Tempo is quarter notes per minute. Converting that index to bars uses the time signature, but the signature does not change how fast samples advance.

```
samples per quarter = sampleRate * 60 / bpm
quarters per beat   = 4 / denominator
quarters per bar    = numerator * quarters per beat
```

Ticks are 960 per quarter note. Display is `bar.beat.tick`, 1-based bar and beat.

Changing tempo or sample rate converts the current sample index to quarter notes with the old mapping and back with the new one, so the musical position stays put. Audio clips are not warped: their files still play at recorded speed, so they cover fewer beats after a tempo increase.

Loop points are stored in quarter notes and converted to samples whenever tempo or sample rate changes. `Transport::advance` splits a block at the loop end and continues from the loop start. The mixer only ever sees a contiguous region.

Stop returns to sample 0. Pause holds the playhead. Play resumes.

The metronome is a pure function of the timeline (a 20 ms sine, accented on bar downbeats), so it cannot drift from the clock. The 440 Hz test tone is free-running and sounds even when the transport is stopped, which is how you check the device before loading a file.

## Document

```
Project
  tempo, meter, loop, master gain
  Track[]  (audio | midi)
    mixer: gain, pan, mute, solo
    Clip    audio: file, start in quarter notes, offset/length in seconds
            midi:  start, length, notes
```

Edits are `EditCommand` objects on `History` (apply / revert, bounded depth). The UI calls them for tempo, meter, loop, tracks, clips, and mixer moves. Transport play / stop is not an edit; it is a command to the audio thread only.

`waveform_core` is standard C++20. It does not link JUCE, which is why the timing and JSON tests do not need a device or a GUI. The engine adapts that model to JUCE's device and file APIs.

JSON version 1 is described in [docs/PROJECT_FORMAT.md](docs/PROJECT_FORMAT.md). Paths inside the project directory are stored with forward slashes so a folder can move between macOS and Windows.

## Playback

On load, the message thread decodes the file with JUCE and linearly resamples it to the device rate. The callback then copies samples 1:1. Phase 0 keeps at most 10 minutes of audio per file in memory.

Each block:

1. Drain transport commands.
2. If playing, advance the clock and mix each contiguous segment.
3. Apply track gain, constant-power pan, mute, and solo (mute wins; any solo silences the rest).
4. Apply master gain.
5. Add metronome and test tone.
6. Hard-clip to [-1, 1] and publish peaks.

`waveform-render` runs steps 2–6 without a device and writes a 16-bit WAV. The demo test uses it at 48 kHz (the file's rate) and 44.1 kHz (resampled).

## Where later work attaches

**Arrangement view.** Clips already have ids and a start on the quarter-note grid. A timeline component draws those clips and seeks by posting `SeekSamples`. It should not grow a second clock.

**Waveform peaks.** Build them in `ClipLibrary` when the file is decoded and store them beside `ClipAudio`. The audio thread never needs them.

**Recording.** The callback already receives input pointers and ignores them. A record arm flag can append into a buffer the message thread installed before the take. Commit the take with `AddAudioClipCommand` when the transport stops.

**MIDI and a piano roll.** `MidiClip` / `MidiNote` and `AddMidiNoteCommand` are the document side. A piano roll edits those commands. Playback needs a new voice object, called from `renderLinearBlock` after the audio clips, with a per-block note list built on the message thread or from a lock-free note queue. Phase 0 stores the notes and does not synthesise them. Soloing a MIDI track silences audio tracks, which is the mixer rule, and the track stays quiet until a voice exists.

**Mixer routing.** `RenderTrack` is one stereo sum into the master. A send is another gain into a bus id, and a bus is another `RenderTrack` processed after its sources. Keep the graph immutable and rebuilt when the routing changes.

**Plug-in hosting.** Add a `TrackProcessor` interface with `prepare` and `process` used after the clip sum and before pan. The JUCE hosting wrappers (VST3, and Audio Units on macOS) implement that interface inside the engine. The document stores the plug-in identifier and state blob in a future file version. Do not put host objects in `waveform_core`. ASIO stays unrelated to plug-in hosting: it is only a Windows device driver, still off unless `WAVEFORM_ENABLE_ASIO` is set.

**Session grid.** Add a `slots` array that references existing clip ids plus a launch mode. The arrangement timeline stays. Launching a slot seeks or retriggers that clip through the same `Transport`. Unknown JSON keys are already ignored, but a real slot model should bump `version` or ship behind an optional key the version-1 reader skips.

**Warp.** Keep the current unwarped path. A warp mode would map timeline quarter notes to source seconds with a breakpoint list evaluated on the message thread into a read cursor the callback can follow. Do not silently change the meaning of `lengthSeconds`.

**Automation.** Sample-accurate breakpoints can live next to clips and be applied inside `renderLinearBlock`. Until then, faders are block-accurate because the graph is rebuilt between callbacks.
