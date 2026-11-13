#pragma once

#include "waveform/project.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace waveform
{

/// Pretty-printed JSON. Key order is not significant. See docs/PROJECT_FORMAT.md.
[[nodiscard]] std::string projectToJson (const Project& project);

/// Returns nullopt and writes `error` when the document is not a version-1 project.
[[nodiscard]] std::optional<Project> projectFromJson (std::string_view json, std::string* error);

} // namespace waveform
