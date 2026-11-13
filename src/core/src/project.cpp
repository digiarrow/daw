#include "waveform/project.hpp"

#include <algorithm>
#include <cstdint>
#include <random>

namespace waveform
{
namespace
{

void appendHex (std::string& out, std::uint64_t value, int digits)
{
    constexpr char kDigits[] = "0123456789abcdef";

    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4)
        out.push_back (kDigits[(value >> shift) & 0x0f]);
}

} // namespace

std::string makeId()
{
    thread_local std::mt19937_64 generator { std::random_device {}() };
    std::uniform_int_distribution<std::uint64_t> distribution;

    std::uint64_t high = distribution (generator);
    std::uint64_t low = distribution (generator);
    // RFC 4122 version 4 in the high word, variant 10xxxxxx in the low word.
    high = (high & 0xffffffffffff0fffULL) | 0x0000000000004000ULL;
    low = (low & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;

    std::string id;
    id.reserve (36);
    appendHex (id, (high >> 32) & 0xffffffffULL, 8);
    id.push_back ('-');
    appendHex (id, (high >> 16) & 0xffffULL, 4);
    id.push_back ('-');
    appendHex (id, high & 0xffffULL, 4);
    id.push_back ('-');
    appendHex (id, (low >> 48) & 0xffffULL, 4);
    id.push_back ('-');
    appendHex (id, low & 0xffffffffffffULL, 12);
    return id;
}

const std::string& clipId (const Clip& clip)
{
    return std::visit ([] (const auto& concrete) -> const std::string& { return concrete.id; }, clip);
}

Track* findTrack (Project& project, const std::string& id)
{
    const auto it = std::find_if (project.tracks.begin(), project.tracks.end(),
                                  [&] (const Track& track) { return track.id == id; });
    return it == project.tracks.end() ? nullptr : &(*it);
}

const Track* findTrack (const Project& project, const std::string& id)
{
    const auto it = std::find_if (project.tracks.begin(), project.tracks.end(),
                                  [&] (const Track& track) { return track.id == id; });
    return it == project.tracks.end() ? nullptr : &(*it);
}

void clampMixer (MixerState& mixer) noexcept
{
    if (std::isnan (mixer.gainDb))
        mixer.gainDb = 0.0;

    mixer.gainDb = std::clamp (mixer.gainDb, -96.0, 12.0);

    if (std::isnan (mixer.pan))
        mixer.pan = 0.0;

    mixer.pan = std::clamp (mixer.pan, -1.0, 1.0);
}

Project Project::makeDefault()
{
    Project project;
    project.name = "Untitled";
    project.tempoBpm = 120.0;
    project.timeSignature = TimeSignature { 4, 4 };
    project.loopEnabled = false;
    project.loopStartQuarterNotes = 0.0;
    project.loopEndQuarterNotes = 8.0;

    Track audio;
    audio.id = makeId();
    audio.name = "Audio 1";
    audio.type = TrackType::Audio;
    project.tracks.push_back (std::move (audio));

    Track midi;
    midi.id = makeId();
    midi.name = "MIDI 1";
    midi.type = TrackType::Midi;
    project.tracks.push_back (std::move (midi));
    return project;
}

} // namespace waveform
