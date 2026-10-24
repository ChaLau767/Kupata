// Kupata — interface
// Copyright (C) 2026 Lautaro Martín Vieyra
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace kupata::ui
{
namespace colours
{
    const juce::Colour body      { 0xff34332f };   // the moulded case
    const juce::Colour bodyEdge  { 0xff47453f };
    const juce::Colour well      { 0xff252420 };   // recesses: pads, fields
    const juce::Colour line      { 0xff4f4c45 };
    const juce::Colour text      { 0xffece5d3 };   // bone
    const juce::Colour sub       { 0xffa9a396 };
    const juce::Colour faint     { 0xff76726a };
    const juce::Colour lcd       { 0xffc9c5b3 };   // the screen: dark ink on a pale glass
    const juce::Colour lcdEdge   { 0xff9c9887 };
    const juce::Colour ink       { 0xff24231f };
    const juce::Colour inkDim    { 0xff8d897b };
    const juce::Colour accent    { 0xffef8b23 };
    // the four rows of pads, from the bottom: vermilion, tangerine, mustard, bone
    inline juce::Colour row (int pad)
    {
        static const juce::Colour c[] { juce::Colour (0xffe2492e), juce::Colour (0xffef8b23), juce::Colour (0xffe6c13d), juce::Colour (0xffe9e2cf) };
        return c[juce::jlimit (0, 3, pad / 4)];
    }
}

juce::Font sans (float height);
juce::Font semi (float height);
juce::Font heavy (float height);
juce::Font mono (float height);
juce::Font monoBold (float height);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return mono (12.5f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return sans (14.0f); }
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
};

//==============================================================================
// A knob for one parameter of the selected pad, with its name and value underneath.
class Knob : public juce::Component
{
public:
    Knob (const FloatSpec&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setLanguage (Lang);
    void attach (juce::AudioProcessorValueTreeState&, const juce::String& paramId);
    void setColour (juce::Colour c) { slider.getProperties().set ("arc", (juce::int64) c.getARGB()); slider.repaint(); }
    std::function<juce::String (float)> extraText;   // a second line under the value
    juce::Slider slider;
    static constexpr int width = 104, dial = 66, height = 118;

private:
    const FloatSpec& spec;
    Lang lang = Lang::en;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A number in a well that changes when dragged up or down (output, velocity, note).
class DragValue : public juce::Component, public juce::SettableTooltipClient
{
public:
    DragValue() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::function<float()> get;
    std::function<void (float)> set;
    std::function<juce::String (float)> text;
    std::function<void (bool)> gesture;
    float min = 0.0f, max = 1.0f, def = 0.0f, pixelsPerUnit = 2.0f, step = 0.0f;
    juce::String label;

private:
    float startValue = 0.0f;
};

// A row of mutually exclusive buttons.
class Segmented : public juce::Component, public juce::SettableTooltipClient
{
public:
    Segmented (std::vector<juce::String> labels, std::function<int()> get, std::function<void (int)> set);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setLabels (std::vector<juce::String> l) { labels = std::move (l); repaint(); }
    juce::Colour onColour = colours::accent;

private:
    std::vector<juce::String> labels;
    std::function<int()> get;
    std::function<void (int)> set;
};

//==============================================================================
// The sixteen pads, four by four, pad 1 at the bottom left.
class PadGrid : public juce::Component, public juce::SettableTooltipClient,
                public juce::DragAndDropTarget, public juce::FileDragAndDropTarget
{
public:
    explicit PadGrid (KupataProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override { dropTarget = -1; repaint(); }
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override { dropTarget = -1; repaint(); }
    void filesDropped (const juce::StringArray&, int x, int y) override;

    void update (const Telemetry&, Lang);
    void refresh() { mini.clear(); repaint(); }
    int padAt (juce::Point<float>) const;
    juce::Rectangle<float> cell (int pad) const;

    std::function<void (int)> onSelect;
    std::function<void (int)> onMenu;
    std::function<void (int, const juce::StringArray&)> onFiles;   // pad, files
    std::function<void (int, int, bool)> onMove;                    // from, to, copy

private:
    const std::vector<float>& miniWave (int pad, const KupataProcessor::PadInfo&);
    KupataProcessor& processor;
    Telemetry tele;
    Lang lang = Lang::en;
    int pressed = -1, hover = -1, dropTarget = -1;
    bool dragging = false;
    std::map<const Sound*, std::vector<float>> mini;
};

// The screen: the selected pad's sound, as it is and as it sounds now, with its trim markers.
class Screen : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Screen (KupataProcessor&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void setPad (int pad, const KupataProcessor::PadInfo&, const PadParams&, Lang);
    void update (const Telemetry& t) { tele = t; repaint(); }
    std::function<void()> onLoadRequest;

private:
    juce::Rectangle<float> waveArea() const;
    int markerAt (juce::Point<float>) const;   // 0 start, 1 end, -1 none
    void computePeaks();
    KupataProcessor& processor;
    KupataProcessor::PadInfo info;
    PadParams params;
    int pad = 0, dragMarker = -1, hoverMarker = -1;
    Lang lang = Lang::en;
    Telemetry tele;
    std::vector<float> rendered, peaksOriginal, peaksRendered;
    const Sound* peaksFor = nullptr;
    bool renderedValid = false;
    double renderedSeconds = 0.0;
};

// Hit picker, for a sound with several hits.
class HitBar : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit HitBar (KupataProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void setPad (int pad, const KupataProcessor::PadInfo&, Lang);
    std::function<void()> onChange;

private:
    KupataProcessor& processor;
    juce::TextButton prev, next, whole, toPads;
    int pad = 0, numHits = 1, hit = 0;
    Lang lang = Lang::en;
};

// The file browser.
class Browser : public juce::Component, private juce::ListBoxModel
{
public:
    explicit Browser (KupataProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setLanguage (Lang);
    void setFolder (const juce::File&);
    std::function<void (const juce::File&)> onLoad;

private:
    int getNumRows() override { return items.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { open (row); }
    void returnKeyPressed (int row) override { open (row); }
    void open (int row);
    void selectedRowsChanged (int row) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    juce::String getTooltipForRow (int) override;

    KupataProcessor& processor;
    juce::File folder;
    juce::Array<juce::File> items;
    juce::ListBox list { {}, this };
    juce::TextButton up, choose;
    std::unique_ptr<juce::FileChooser> chooser;
    Lang lang = Lang::en;
    bool clicking = false;
};

//==============================================================================
class Content : public juce::Component, public juce::DragAndDropContainer, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit Content (KupataProcessor&);
    ~Content() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    static constexpr int baseWidth = 1180, baseHeight = 720;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }

private:
    void timerCallback() override;
    void setLanguage (Lang);
    void selectPad (int pad);
    void refreshPad (bool force);
    void attachPad (int pad);
    void showPadMenu (int pad);
    void showKitMenu();
    void showNotesMenu();
    void saveKitDialog();
    void chooseFile (int pad);
    void loadFiles (int pad, const juce::StringArray&);
    void fillOutputs();
    void updateTexts();

    KupataProcessor& processor;
    LookAndFeel lnf;
    Lang lang = Lang::en;

    juce::TextButton langEn { "EN" }, langEs { "ES" }, kitButton, notesButton, multiOutButton;
    DragValue output, velocity;
    Browser browser;
    PadGrid pads;
    Screen screen;
    HitBar hitBar;
    juce::OwnedArray<Knob> knobs;
    juce::TextButton gateButton, muteButton, soloButton, monoButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gateAttachment, muteAttachment, soloAttachment, monoAttachment;
    juce::ComboBox chokeBox, outBox;
    Segmented stereoSwitch;
    DragValue noteValue;
    juce::TextButton learnButton;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> dialog;
    int selected = -1, shownVersion = -1, shownOutputs = -1, attachedPad = -1;
    int copiedPad = -1;
    bool dropHover = false;
    juce::String message;
    int messageTicks = 0;
    Telemetry telemetry;
};

} // namespace kupata::ui

class KupataEditor : public juce::AudioProcessorEditor
{
public:
    explicit KupataEditor (KupataProcessor&);
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (kupata::ui::colours::body); }

private:
    kupata::ui::Content content;
    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KupataEditor)
};
