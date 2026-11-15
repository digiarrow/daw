#pragma once

#include <string>
#include <string_view>

namespace waveform
{

/// Browser and import extensions. MP3 is decoded by JUCE when JUCE_USE_MP3AUDIOFORMAT is on.
[[nodiscard]] inline bool isImportableAudioExtension (std::string_view extension) noexcept
{
    auto lower = std::string (extension);

    for (auto& character : lower)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char> (character - 'A' + 'a');

    if (! lower.empty() && lower.front() == '.')
        lower.erase (lower.begin());

    return lower == "wav" || lower == "aif" || lower == "aiff" || lower == "flac" || lower == "ogg" || lower == "mp3";
}

} // namespace waveform
