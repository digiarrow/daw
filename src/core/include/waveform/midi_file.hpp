#pragma once

#include "waveform/project.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace waveform
{

/// Standard MIDI file, format 0, 960 ticks per quarter note.
[[nodiscard]] std::vector<std::uint8_t> midiClipToSmf (const MidiClip& clip);

struct SmfImport
{
    bool ok = false;
    std::string error;
    MidiClip clip;
};

[[nodiscard]] SmfImport midiClipFromSmf (const std::uint8_t* bytes, std::size_t size);

} // namespace waveform
