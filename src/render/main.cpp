#include "OfflineRenderer.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace
{

#if defined (_WIN32)
std::string wideToUtf8 (const wchar_t* text)
{
    const auto length = static_cast<int> (wcslen (text));

    if (length <= 0)
        return {};

    const int needed = WideCharToMultiByte (CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string utf8 (static_cast<std::size_t> (needed), '\0');
    WideCharToMultiByte (CP_UTF8, 0, text, length, utf8.data(), needed, nullptr, nullptr);
    return utf8;
}
#endif

std::vector<std::string> arguments (int argc, char** argv)
{
#if defined (_WIN32)
    (void) argc;
    (void) argv;
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW (GetCommandLineW(), &count);
    std::vector<std::string> args;

    if (wide == nullptr)
        return args;

    args.reserve (static_cast<std::size_t> (count));

    for (int index = 0; index < count; ++index)
        args.push_back (wideToUtf8 (wide[index]));

    LocalFree (wide);
    return args;
#else
    std::vector<std::string> args;
    args.reserve (static_cast<std::size_t> (argc));

    for (int index = 0; index < argc; ++index)
        args.emplace_back (argv[index] != nullptr ? argv[index] : "");

    return args;
#endif
}

void printUsage()
{
    std::cout << "waveform-render --project file.json --output out.wav\n"
                 "                 [--seconds 4] [--sample-rate 48000]\n"
                 "                 [--metronome] [--test-tone]\n";
}

} // namespace

int main (int argc, char** argv)
{
    const auto args = arguments (argc, argv);
    waveform::OfflineRenderRequest request;

    for (std::size_t index = 1; index < args.size(); ++index)
    {
        const auto& argument = args[index];
        auto requireValue = [&] (const char* name) -> std::string
        {
            if (index + 1 >= args.size())
            {
                std::cerr << "missing value for " << name << '\n';
                std::exit (2);
            }

            return args[++index];
        };

        if (argument == "--project")
            request.projectPath = requireValue ("--project");
        else if (argument == "--output")
            request.outputPath = requireValue ("--output");
        else if (argument == "--seconds")
            request.seconds = std::stod (requireValue ("--seconds"));
        else if (argument == "--sample-rate")
            request.sampleRate = std::stod (requireValue ("--sample-rate"));
        else if (argument == "--metronome")
            request.metronome = true;
        else if (argument == "--test-tone")
            request.testTone = true;
        else if (argument == "--help" || argument == "-h")
        {
            printUsage();
            return 0;
        }
        else
        {
            std::cerr << "unknown argument " << argument << '\n';
            printUsage();
            return 2;
        }
    }

    if (request.projectPath.empty() || request.outputPath.empty())
    {
        std::cerr << "pass both --project and --output\n";
        printUsage();
        return 2;
    }

    const auto result = waveform::renderProjectFile (request);
    (result.ok ? std::cout : std::cerr) << result.message << '\n';
    return result.exitCode;
}
