#include "waveform/paths.hpp"

#include <system_error>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace waveform
{
namespace
{

std::string forwardSlashes (std::string path)
{
    for (char& character : path)
        if (character == '\\')
            character = '/';

    return path;
}

bool escapesDirectory (const std::string& relative)
{
    return relative == ".."
        || relative.rfind ("../", 0) == 0
        || relative.rfind ("..\\", 0) == 0;
}

} // namespace

std::filesystem::path pathFromUtf8 (std::string_view utf8)
{
#if defined (_WIN32)
    if (utf8.empty())
        return {};

    const int needed = MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS,
                                            utf8.data(), static_cast<int> (utf8.size()),
                                            nullptr, 0);

    if (needed <= 0)
        return std::filesystem::path (std::string (utf8));

    std::wstring wide (static_cast<std::size_t> (needed), L'\0');
    MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS,
                         utf8.data(), static_cast<int> (utf8.size()),
                         wide.data(), needed);
    return std::filesystem::path (wide);
#else
    return std::filesystem::path (std::string (utf8));
#endif
}

std::string pathToUtf8 (const std::filesystem::path& path)
{
#if defined (_WIN32)
    const std::wstring wide = path.wstring();

    if (wide.empty())
        return {};

    const int needed = WideCharToMultiByte (CP_UTF8, 0, wide.data(), static_cast<int> (wide.size()),
                                            nullptr, 0, nullptr, nullptr);

    if (needed <= 0)
        return {};

    std::string utf8 (static_cast<std::size_t> (needed), '\0');
    WideCharToMultiByte (CP_UTF8, 0, wide.data(), static_cast<int> (wide.size()),
                         utf8.data(), needed, nullptr, nullptr);
    return utf8;
#else
    return path.string();
#endif
}

std::string portableAudioPath (const std::filesystem::path& projectFile,
                               const std::filesystem::path& audioFile)
{
    const auto absoluteAudio = pathToUtf8 (audioFile);
    std::error_code error;
    const auto relative = std::filesystem::relative (audioFile, projectFile.parent_path(), error);

    if (error || relative.empty())
        return absoluteAudio;

    auto text = forwardSlashes (pathToUtf8 (relative));

    if (escapesDirectory (text) || text.find (":/") != std::string::npos)
        return absoluteAudio;

    return text;
}

std::filesystem::path resolveAudioPath (const std::filesystem::path& projectFile,
                                        const std::string& storedPath)
{
    const auto stored = pathFromUtf8 (storedPath);

    if (stored.is_absolute())
        return stored;

    return projectFile.parent_path() / stored;
}

} // namespace waveform
