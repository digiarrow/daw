#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

class BrowserPanel : public juce::Component
{
public:
    BrowserPanel();
    ~BrowserPanel() override;
    void setFolder (const juce::File& folder);
    [[nodiscard]] const juce::File& folder() const noexcept { return folder_; }

    std::function<void()> onChooseFolder;
    std::function<void (const juce::String& path)> onPreview;

    void resized() override;
    void paint (juce::Graphics& graphics) override;

private:
    class Row;

    void rebuild();

    juce::File folder_;
    juce::Label title_;
    juce::TextEditor search_;
    juce::TextButton folderButton_ { "Folder" };
    juce::Viewport viewport_;
    juce::Component list_;
    std::vector<std::unique_ptr<Row>> rows_;
};
