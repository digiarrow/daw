#include "waveform/midi_file.hpp"

#include <algorithm>
#include <cstring>

namespace waveform
{
namespace
{

constexpr int kTicksPerQuarter = 960;

void writeU16 (std::vector<std::uint8_t>& out, unsigned value)
{
    out.push_back (static_cast<std::uint8_t> ((value >> 8) & 0xff));
    out.push_back (static_cast<std::uint8_t> (value & 0xff));
}

void writeU32 (std::vector<std::uint8_t>& out, unsigned value)
{
    out.push_back (static_cast<std::uint8_t> ((value >> 24) & 0xff));
    out.push_back (static_cast<std::uint8_t> ((value >> 16) & 0xff));
    out.push_back (static_cast<std::uint8_t> ((value >> 8) & 0xff));
    out.push_back (static_cast<std::uint8_t> (value & 0xff));
}

void writeVarLen (std::vector<std::uint8_t>& out, unsigned value)
{
    unsigned buffer = value & 0x7fu;

    while ((value >>= 7) != 0)
    {
        buffer <<= 8;
        buffer |= (value & 0x7fu) | 0x80u;
    }

    while (true)
    {
        out.push_back (static_cast<std::uint8_t> (buffer & 0xffu));

        if ((buffer & 0x80u) != 0)
            buffer >>= 8;
        else
            break;
    }
}

unsigned readU16 (const std::uint8_t* bytes, std::size_t size, std::size_t& cursor, bool& ok)
{
    if (cursor + 2 > size)
    {
        ok = false;
        return 0;
    }

    const unsigned value = (static_cast<unsigned> (bytes[cursor]) << 8) | bytes[cursor + 1];
    cursor += 2;
    return value;
}

unsigned readU32 (const std::uint8_t* bytes, std::size_t size, std::size_t& cursor, bool& ok)
{
    if (cursor + 4 > size)
    {
        ok = false;
        return 0;
    }

    const unsigned value = (static_cast<unsigned> (bytes[cursor]) << 24)
                         | (static_cast<unsigned> (bytes[cursor + 1]) << 16)
                         | (static_cast<unsigned> (bytes[cursor + 2]) << 8)
                         | bytes[cursor + 3];
    cursor += 4;
    return value;
}

unsigned readVarLen (const std::uint8_t* bytes, std::size_t size, std::size_t& cursor, bool& ok)
{
    unsigned value = 0;

    for (int byte = 0; byte < 4; ++byte)
    {
        if (cursor >= size)
        {
            ok = false;
            return 0;
        }

        const unsigned current = bytes[cursor++];
        value = (value << 7) | (current & 0x7fu);

        if ((current & 0x80u) == 0)
            return value;
    }

    ok = false;
    return 0;
}

struct Event
{
    unsigned tick = 0;
    int note = 60;
    int velocity = 0;
    int channel = 1;
    bool noteOff = false;
};

} // namespace

std::vector<std::uint8_t> midiClipToSmf (const MidiClip& clip)
{
    std::vector<Event> events;
    events.reserve (clip.notes.size() * 2);

    for (const auto& note : clip.notes)
    {
        const auto startTick = static_cast<unsigned> (std::max (0.0, note.startQuarterNotes) * kTicksPerQuarter);
        const auto endTick = static_cast<unsigned> (std::max (note.startQuarterNotes, note.startQuarterNotes + note.lengthQuarterNotes) * kTicksPerQuarter);
        Event on;
        on.tick = startTick;
        on.note = std::clamp (note.note, 0, 127);
        on.velocity = std::clamp (note.velocity, 1, 127);
        on.channel = std::clamp (note.channel, 1, 16);
        events.push_back (on);
        Event off = on;
        off.tick = std::max (endTick, startTick + 1);
        off.velocity = 0;
        off.noteOff = true;
        events.push_back (off);
    }

    std::sort (events.begin(), events.end(), [] (const Event& left, const Event& right)
    {
        if (left.tick != right.tick)
            return left.tick < right.tick;

        return left.noteOff && ! right.noteOff;
    });

    std::vector<std::uint8_t> track;
    unsigned previous = 0;

    for (const auto& event : events)
    {
        writeVarLen (track, event.tick - previous);
        previous = event.tick;
        const auto status = static_cast<std::uint8_t> ((event.noteOff ? 0x80 : 0x90) | (event.channel - 1));
        track.push_back (status);
        track.push_back (static_cast<std::uint8_t> (event.note));
        track.push_back (static_cast<std::uint8_t> (event.velocity));
    }

    writeVarLen (track, 0);
    track.push_back (0xff);
    track.push_back (0x2f);
    track.push_back (0x00);

    std::vector<std::uint8_t> file;
    file.insert (file.end(), { 'M', 'T', 'h', 'd' });
    writeU32 (file, 6);
    writeU16 (file, 0);
    writeU16 (file, 1);
    writeU16 (file, kTicksPerQuarter);
    file.insert (file.end(), { 'M', 'T', 'r', 'k' });
    writeU32 (file, static_cast<unsigned> (track.size()));
    file.insert (file.end(), track.begin(), track.end());
    return file;
}

SmfImport midiClipFromSmf (const std::uint8_t* bytes, std::size_t size)
{
    SmfImport result;
    result.clip.id = makeId();
    result.clip.name = "Imported";
    bool ok = true;
    std::size_t cursor = 0;

    if (bytes == nullptr || size < 22 || std::memcmp (bytes, "MThd", 4) != 0)
    {
        result.error = "not a standard MIDI file";
        return result;
    }

    cursor = 4;
    const unsigned headerLength = readU32 (bytes, size, cursor, ok);
    const unsigned format = readU16 (bytes, size, cursor, ok);
    const unsigned tracks = readU16 (bytes, size, cursor, ok);
    const unsigned division = readU16 (bytes, size, cursor, ok);

    if (! ok || headerLength < 6 || division == 0 || (division & 0x8000u) != 0)
    {
        result.error = "MIDI header is incomplete";
        return result;
    }

    if (format > 1 || tracks == 0)
    {
        result.error = "only MIDI format 0 and 1 are read";
        return result;
    }

    cursor = 8 + headerLength;
    std::vector<Event> noteOns;

    for (unsigned trackIndex = 0; trackIndex < tracks && cursor + 8 <= size; ++trackIndex)
    {
        if (std::memcmp (bytes + cursor, "MTrk", 4) != 0)
        {
            result.error = "MIDI track chunk is missing";
            return result;
        }

        cursor += 4;
        const unsigned length = readU32 (bytes, size, cursor, ok);
        const std::size_t trackEnd = cursor + length;

        if (! ok || trackEnd > size)
        {
            result.error = "MIDI track is truncated";
            return result;
        }

        unsigned tick = 0;
        unsigned running = 0;

        while (ok && cursor < trackEnd)
        {
            tick += readVarLen (bytes, size, cursor, ok);

            if (! ok || cursor >= trackEnd)
                break;

            unsigned status = bytes[cursor];

            if ((status & 0x80u) != 0)
            {
                running = status;
                ++cursor;
            }
            else
            {
                status = running;
            }

            if ((status & 0xf0u) == 0x90 || (status & 0xf0u) == 0x80)
            {
                if (cursor + 1 >= trackEnd)
                {
                    ok = false;
                    break;
                }

                const int note = bytes[cursor++];
                const int velocity = bytes[cursor++];
                const bool noteOff = (status & 0xf0u) == 0x80 || velocity == 0;
                const int channel = static_cast<int> (status & 0x0fu) + 1;

                if (! noteOff)
                {
                    Event on;
                    on.tick = tick;
                    on.note = note;
                    on.velocity = std::max (1, velocity);
                    on.channel = channel;
                    noteOns.push_back (on);
                }
                else
                {
                    for (auto it = noteOns.rbegin(); it != noteOns.rend(); ++it)
                    {
                        if (it->note == note && it->channel == channel && ! it->noteOff && it->velocity > 0)
                        {
                            MidiNote stored;
                            stored.note = note;
                            stored.velocity = it->velocity;
                            stored.channel = channel;
                            stored.startQuarterNotes = static_cast<double> (it->tick) / division;
                            stored.lengthQuarterNotes = std::max (1.0 / division, static_cast<double> (tick - it->tick) / division);
                            result.clip.notes.push_back (stored);
                            it->velocity = 0;
                            break;
                        }
                    }
                }
            }
            else if ((status & 0xf0u) == 0xc0 || (status & 0xf0u) == 0xd0)
            {
                if (cursor >= trackEnd)
                    ok = false;
                else
                    ++cursor;
            }
            else if (status == 0xff)
            {
                if (cursor >= trackEnd)
                {
                    ok = false;
                    break;
                }

                ++cursor;
                const unsigned dataLength = readVarLen (bytes, size, cursor, ok);
                cursor += dataLength;
            }
            else if ((status & 0x80u) != 0)
            {
                if (cursor + 1 >= trackEnd)
                    ok = false;
                else
                    cursor += 2;
            }
            else
            {
                ok = false;
            }
        }

        cursor = trackEnd;
    }

    if (! ok)
    {
        result.error = "MIDI track could not be read";
        result.clip.notes.clear();
        return result;
    }

    double length = 1.0;

    for (const auto& note : result.clip.notes)
        length = std::max (length, note.startQuarterNotes + note.lengthQuarterNotes);

    result.clip.lengthQuarterNotes = length;
    result.ok = true;
    return result;
}

} // namespace waveform
