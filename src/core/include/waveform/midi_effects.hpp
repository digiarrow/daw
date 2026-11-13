#pragma once

#include "waveform/project.hpp"
#include "waveform/synth.hpp"

namespace waveform
{

inline constexpr int kMidiEffectSlots = 4;

[[nodiscard]] const char* midiEffectKindName (MidiEffectKind kind) noexcept;
[[nodiscard]] bool midiEffectKindFromName (const std::string& name, MidiEffectKind& kind) noexcept;
[[nodiscard]] const char* scaleKindName (ScaleKind kind) noexcept;
[[nodiscard]] bool scaleKindFromName (const std::string& name, ScaleKind& kind) noexcept;
[[nodiscard]] const char* chordKindName (ChordKind kind) noexcept;
[[nodiscard]] bool chordKindFromName (const std::string& name, ChordKind& kind) noexcept;

/// Writes the chain into `output`. No allocation. Effects run in order, before the instrument.
[[nodiscard]] int applyMidiEffects (const MidiNote* input,
                                    int inputCount,
                                    const MidiEffect* effects,
                                    int effectCount,
                                    double clipLengthQuarters,
                                    MidiNote* output,
                                    int outputCapacity) noexcept;

/// Live notes, shaped by the same chain. Notes that are silent at `secondsHeld` are dropped.
/// `secondsHeld` is how long the oldest held note has been down.
[[nodiscard]] int shapeHeldNotes (const SoundingNote* held,
                                  int heldCount,
                                  const MidiEffect* effects,
                                  int effectCount,
                                  double secondsHeld,
                                  double bpm,
                                  SoundingNote* output,
                                  int outputCapacity) noexcept;

} // namespace waveform
