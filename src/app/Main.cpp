#include "MainComponent.hpp"
#include "StudioLookAndFeel.hpp"
#include "waveform/version.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow()
        : DocumentWindow (waveform::kProductName,
                          juce::Colour (waveform::StudioLookAndFeel::kBackground),
                          DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), true);
        setResizable (true, true);
        setResizeLimits (820, 560, 2400, 1600);
        centreWithSize (1040, 720);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class WaveformStudioApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return waveform::kProductName; }
    const juce::String getApplicationVersion() override { return waveform::kVersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        lookAndFeel_ = std::make_unique<waveform::StudioLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel_.get());
        mainWindow_ = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        mainWindow_ = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel_ = nullptr;
    }

private:
    std::unique_ptr<waveform::StudioLookAndFeel> lookAndFeel_;
    std::unique_ptr<MainWindow> mainWindow_;
};

} // namespace

START_JUCE_APPLICATION (WaveformStudioApplication)
