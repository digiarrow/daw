# Waveform Studio project format

Version 1. Files are UTF-8 JSON. Key order does not matter. Unknown keys are ignored so a later version can add optional fields without breaking a Phase 0 reader, but `version` must be `1` or the file is rejected.

```json
{
  "format": "waveform-studio-project",
  "version": 1,
  "name": "Phase 0 Demo",
  "tempo": 120.0,
  "timeSignature": { "numerator": 4, "denominator": 4 },
  "loop": {
    "enabled": true,
    "startQuarterNotes": 0.0,
    "endQuarterNotes": 8.0
  },
  "master": { "gainDb": 0.0 },
  "tracks": []
}
```

## Clock

`tempo` is quarter notes per minute, from 20 to 999. Values outside that range are clamped on load.

`timeSignature.denominator` is the note that gets one beat and must be a power of two from 1 to 32. `numerator` is from 1 to 32. A bar of 4/4 is 4 quarter notes. A bar of 6/8 is 3 quarter notes (six eighths).

`loop.startQuarterNotes` and `loop.endQuarterNotes` are positions on that same quarter-note grid. The end must be greater than the start. While the loop is enabled, the playhead wraps from the end back to the start. Playback before the start runs forward until it hits the end, then wraps.

## Tracks

`type` is `"audio"` or `"midi"`.

`mixer`:

| Field | Meaning |
| --- | --- |
| `gainDb` | Fader, clamped to -96..+12 dB |
| `pan` | -1 hard left, 0 centre, +1 hard right. Centre is constant-power (-3 dB each side) |
| `mute` | Silence this track |
| `solo` | If any track is soloed, tracks that are not soloed are silent. Mute still wins |

`master.gainDb` uses the same decibel range. Master pan, mute, and solo are not used.

`countInBars` is optional and defaults to 0 when it is absent. New projects start at 1. It is the number of bars of metronome heard before a recording punch-in. Values are clamped to 0..8.

A MIDI track may include a `synth` object for the built-in instrument. If it is absent, the track uses the default saw patch.

```json
"synth": {
  "wave": "saw",
  "detuneCents": 6.0,
  "cutoffHz": 1800.0,
  "resonance": 0.2,
  "attack": 0.01,
  "decay": 0.12,
  "sustain": 0.7,
  "release": 0.2
}
```

`wave` is `sine`, `saw`, `square`, or `triangle`.

## Clips

Clips live in `tracks[].clips` in timeline order. Each clip has a `type`.

Audio clips are **not warped**. The file plays at its recorded speed. `startQuarterNotes` is where the clip sits on the musical timeline. `offsetSeconds` and `lengthSeconds` choose a region of the source. The clip's length in beats changes when the tempo changes, because one second of audio is fewer beats at a higher tempo. Time-stretch is a later phase and will be a separate mode.

```json
{
  "type": "audio",
  "id": "clip-demo-tone",
  "name": "Arpeggio",
  "file": "demo-tone.wav",
  "startQuarterNotes": 0.0,
  "offsetSeconds": 0.0,
  "lengthSeconds": 4.0,
  "gainDb": 0.0
}
```

`file` is either absolute, or relative to the directory that contains the project file, using forward slashes. Saving a project rewrites paths that live inside that directory as relative paths so the same folder can move between macOS and Windows.

MIDI clips store notes and the built-in polysynth plays them. `note` timing is in quarter notes from the start of the clip.

```json
{
  "type": "midi",
  "id": "clip-midi-sketch",
  "name": "Sketch",
  "startQuarterNotes": 0.0,
  "lengthQuarterNotes": 4.0,
  "notes": [
    {
      "note": 69,
      "startQuarterNotes": 0.0,
      "lengthQuarterNotes": 1.0,
      "velocity": 96,
      "channel": 1
    }
  ]
}
```

`note` is 0..127, `velocity` is 1..127, `channel` is 1..16. Note starts are quarter notes from the beginning of the clip.

## Identity

Every track and clip `id` is a non-empty string and must be unique in the file. The app generates UUID strings. Hand-written ids are fine.

## What this version does not store

Device setup, undo history, plugin state, automation, routing, warp markers, or session-grid slots. Those can be added under new keys or a later `version`.
