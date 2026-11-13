#pragma once

#include "waveform/project.hpp"

#include <filesystem>
#include <string>

namespace waveform

{

struct CollectResult
{
    int copied = 0;
    int alreadyInside = 0;
    std::vector<std::string> missing;
};

/// Copies clip and sampler files into `<project>/Media` and rewrites the project to relative paths.
[[nodiscard]] CollectResult collectMedia (Project& project, const std::filesystem::path& projectFile);

} // namespace waveform
