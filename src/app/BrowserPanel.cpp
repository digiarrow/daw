#include "BrowserPanel.hpp"

#include "StudioLookAndFeel.hpp"
#include "waveform/media.hpp"
#include "waveform/phase8.hpp"

#include <map>

namespace
{

bool isAudio (const juce::File& file)
{
    return waveform::isImportableAudioExtension (file.getFileExtension().toStdString());
}

} // namespace

class BrowserPanel::Row : public juce::Component
{
public:
    Row (juce::File file, std::function<void (const juce::String&)> preview)
        : file_ (std::move (file)), preview_ (std::move (preview))
    {
    }

    void paint (juce::Graphics& graphics) override
    {
        graphics.setColour (juce::Colour (hover_ ? 0xff24352e : waveform::StudioLookAndFeel::kPanel));
        graphics.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f, 1.0f), 4.0f);
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kText));
        graphics.drawText (file_.getFileName(), getLocalBounds().reduced (8, 0), juce::Justification::centredLeft, true);
    }

    void mouseEnter (const juce::MouseEvent&) override { hover_ = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover_ = false; repaint(); }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (dragging_ || event.getDistanceFromDragStart() < 6)
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragging_ = true;
            container->startDragging (file_.getFullPathName(), this);
        }
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (! dragging_ && event.getDistanceFromDragStart() < 6 && preview_)
            preview_ (file_.getFullPathName());

        dragging_ = false;
    }

private:
    juce::File file_;
    std::function<void (const juce::String&)> preview_;
    bool hover_ = false;
    bool dragging_ = false;
};

BrowserPanel::~BrowserPanel() = default;

BrowserPanel::BrowserPanel()
{
    title_.setText ("Browser", juce::dontSendNotification);
    title_.setFont (juce::Font (juce::FontOptions (15.0f)));
    addAndMakeVisible (title_);
    search_.setTextToShowWhenEmpty ("Search name or tag", juce::Colour (waveform::StudioLookAndFeel::kDim));
    search_.onTextChange = [this] { rebuild(); };
    addAndMakeVisible (search_);
    addAndMakeVisible (folderButton_);
    addAndMakeVisible (viewport_);
    viewport_.setViewedComponent (&list_, false);
    viewport_.setScrollBarsShown (true, false);
    folderButton_.onClick = [this]
    {
        if (onChooseFolder)
            onChooseFolder();
    };
    setFolder (juce::File::getSpecialLocation (juce::File::userMusicDirectory));
}

void BrowserPanel::setFolder (const juce::File& folder)
{
    folder_ = folder;
    title_.setText (folder_.exists() ? folder_.getFileName() : "Browser", juce::dontSendNotification);
    rebuild();
}

void BrowserPanel::rebuild()
{
    rows_.clear();
    list_.removeAllChildren();

    if (folder_.isDirectory())
    {
        std::map<std::string, std::string> tags;
        const auto tagFile = folder_.getChildFile ("tags.txt");

        if (tagFile.existsAsFile())
        {
            for (const auto& line : juce::StringArray::fromLines (tagFile.loadFileAsString()))
            {
                const auto trimmed = line.trim();

                if (trimmed.isEmpty() || trimmed.startsWithChar ('#'))
                    continue;

                const auto space = trimmed.indexOfChar (' ');
                const auto name = (space < 0 ? trimmed : trimmed.substring (0, space)).toStdString();
                const auto words = (space < 0 ? juce::String() : trimmed.substring (space + 1)).toStdString();
                tags[name] = words;
            }
        }

        juce::Array<juce::File> files;
        folder_.findChildFiles (files, juce::File::findFiles, false);
        files.sort();
        const auto query = search_.getText().toStdString();

        for (const auto& file : files)
        {
            if (! isAudio (file))
                continue;

            const auto name = file.getFileName().toStdString();
            const auto found = tags.find (name);
            const waveform::MediaRecord record { name.c_str(), found == tags.end() ? nullptr : found->second.c_str() };

            if (! waveform::mediaMatches (record, query.c_str()))
                continue;

            auto row = std::make_unique<Row> (file, onPreview);
            list_.addAndMakeVisible (row.get());
            rows_.push_back (std::move (row));
        }
    }

    resized();
}

void BrowserPanel::paint (juce::Graphics& graphics)
{
    graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kPanel));
    graphics.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    if (rows_.empty())
    {
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kDim));
        graphics.drawText ("No audio files in this folder.", viewport_.getBounds(), juce::Justification::centred);
    }
}

void BrowserPanel::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto header = area.removeFromTop (26);
    folderButton_.setBounds (header.removeFromRight (72));
    title_.setBounds (header);
    area.removeFromTop (4);
    search_.setBounds (area.removeFromTop (24));
    area.removeFromTop (6);
    viewport_.setBounds (area);
    const int height = 26;
    list_.setSize (std::max (0, viewport_.getWidth() - viewport_.getScrollBarThickness()),
                   std::max (viewport_.getHeight(), static_cast<int> (rows_.size()) * height));

    for (std::size_t index = 0; index < rows_.size(); ++index)
        rows_[index]->setBounds (0, static_cast<int> (index) * height, list_.getWidth(), height);
}
