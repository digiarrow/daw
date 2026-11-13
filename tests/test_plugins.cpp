#include "waveform/plugins.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE ("hosted plug-in formats include VST3 and Audio Units on macOS", "[plugins]")
{
    const auto formats = waveform::hostedPluginFormats();
    REQUIRE_FALSE (formats.empty());
    REQUIRE (formats.front() == waveform::HostedPluginFormat::Vst3);
    REQUIRE (std::string (waveform::hostedPluginFormatName (waveform::HostedPluginFormat::Vst3)) == "VST3");

#if defined(WAVEFORM_HOST_AU)
    REQUIRE (formats.size() == 2);
    REQUIRE (formats.back() == waveform::HostedPluginFormat::AudioUnit);
    REQUIRE (std::string (waveform::hostedPluginFormatName (waveform::HostedPluginFormat::AudioUnit)) == "AudioUnit");
#else
    REQUIRE (formats.size() == 1);
#endif
}
