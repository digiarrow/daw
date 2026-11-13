#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace waveform
{

/// UTF-8 path conversion. On Windows the narrow `std::string` constructor of
/// `std::filesystem::path` is not UTF-8, so project files go through these helpers.
[[nodiscard]] std::filesystem::path pathFromUtf8 (std::string_view utf8);
[[nodiscard]] std::string pathToUtf8 (const std::filesystem::path& path);

/// Path stored in a project file. Relative, with forward slashes, when the audio
/// file lives inside the project directory. Absolute UTF-8 otherwise.
[[nodiscard]] std::string portableAudioPath (const std::filesystem::path& projectFile,
                                             const std::filesystem::path& audioFile);

[[nodiscard]] std::filesystem::path resolveAudioPath (const std::filesystem::path& projectFile,
                                                      const std::string& storedPath);

} // namespace waveform
