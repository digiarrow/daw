#include "waveform/collect.hpp"

#include "waveform/paths.hpp"

#include <fstream>

namespace waveform
{
namespace
{

void copyIntoMedia (std::string& stored,
                    const std::filesystem::path& projectFile,
                    const std::filesystem::path& media,
                    CollectResult& result)
{
    if (stored.empty())
        return;

    const auto source = resolveAudioPath (projectFile, stored);

    if (! std::filesystem::exists (source))
    {
        result.missing.push_back (stored);
        return;
    }

    const auto relative = std::filesystem::relative (source, media.parent_path());
    const auto text = pathToUtf8 (relative);
    std::string portable = text;

    for (char& character : portable)
        if (character == '\\')
            character = '/';

    if (portable.rfind ("Media/", 0) == 0 || portable == "Media")
    {
        stored = portable;
        ++result.alreadyInside;
        return;
    }

    std::filesystem::create_directories (media);
    const auto destination = media / source.filename();
    std::error_code error;

    if (! std::filesystem::exists (destination))
        std::filesystem::copy_file (source, destination, error);

    if (error)
    {
        result.missing.push_back (stored);
        return;
    }

    stored = std::string ("Media/") + pathToUtf8 (source.filename());

    for (char& character : stored)
        if (character == '\\')
            character = '/';

    ++result.copied;
}

} // namespace

CollectResult collectMedia (Project& project, const std::filesystem::path& projectFile)
{
    CollectResult result;
    const auto media = projectFile.parent_path() / "Media";

    for (auto& track : project.tracks)
    {
        for (auto& clip : track.clips)
            if (auto* audio = std::get_if<AudioClip> (&clip))
                copyIntoMedia (audio->file, projectFile, media, result);

        for (auto& zone : track.samplerZones)
            copyIntoMedia (zone.file, projectFile, media, result);
    }

    return result;
}

} // namespace waveform
