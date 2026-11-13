#pragma once

#include <vector>

namespace waveform
{

enum class HostedPluginFormat
{
    Vst3,
    AudioUnit
};

[[nodiscard]] inline const char* hostedPluginFormatName (HostedPluginFormat format) noexcept
{
    return format == HostedPluginFormat::AudioUnit ? "AudioUnit" : "VST3";
}

/// Formats the host scans. Audio Units are macOS-only.
[[nodiscard]] inline std::vector<HostedPluginFormat> hostedPluginFormats()
{
    std::vector<HostedPluginFormat> formats { HostedPluginFormat::Vst3 };

#if defined(WAVEFORM_HOST_AU)
    formats.push_back (HostedPluginFormat::AudioUnit);
#endif

    return formats;
}

} // namespace waveform
