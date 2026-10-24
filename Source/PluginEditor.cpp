// Kupata — interface
// Copyright (C) 2026 Lautaro Martín Vieyra
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginEditor.h"
#include "BinaryData.h"

namespace kupata::ui
{

namespace
{
    struct Typefaces
    {
        juce::Typeface::Ptr regular, semibold, extrabold, mono, monoMedium;
        static Typefaces& get()
        {
            static Typefaces t {
                juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoRegular_ttf, BinaryData::ArchivoRegular_ttfSize),
                juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoSemiBold_ttf, BinaryData::ArchivoSemiBold_ttfSize),
                juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoExtraBold_ttf, BinaryData::ArchivoExtraBold_ttfSize),
                juce::Typeface::createSystemTypefaceFor (BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize),
                juce::Typeface::createSystemTypefaceFor (BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize)
            };
            return t;
        }
    };

    constexpr float pi = juce::MathConstants<float>::pi;

    juce::String secondsText (double s, Lang lang)
    {
        return localNumber (s < 1.0 ? juce::String (juce::roundToInt (s * 1000.0)) + " ms" : juce::String (s, 2) + " s", lang);
    }

    bool isAudioFile (const juce::File& f, juce::AudioFormatManager& formats)
    {
        return formats.findFormatForFileExtension (f.getFileExtension()) != nullptr;
    }

    // the well a control sits in
    void drawWell (juce::Graphics& g, juce::Rectangle<float> r, float radius = 6.0f)
    {
        g.setColour (colours::well);
        g.fillRoundedRectangle (r, radius);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (r.reduced (0.5f).translated (0.0f, 0.5f), radius, 1.0f);
        g.setColour (colours::bodyEdge.withAlpha (0.6f));
        g.drawLine (r.getX() + radius, r.getBottom() + 0.5f, r.getRight() - radius, r.getBottom() + 0.5f, 1.0f);
    }
}

juce::Font sans (float h)     { return juce::Font (juce::FontOptions (Typefaces::get().regular).withHeight (h)); }
juce::Font semi (float h)     { return juce::Font (juce::FontOptions (Typefaces::get().semibold).withHeight (h)); }
juce::Font heavy (float h)    { return juce::Font (juce::FontOptions (Typefaces::get().extrabold).withHeight (h)); }
juce::Font mono (float h)     { return juce::Font (juce::FontOptions (Typefaces::get().mono).withHeight (h)); }
juce::Font monoBold (float h) { return juce::Font (juce::FontOptions (Typefaces::get().monoMedium).withHeight (h)); }

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, colours::well);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::headerTextColourId, colours::sub);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, colours::ink);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::backgroundColourId, colours::well);
    setColour (juce::ComboBox::outlineColourId, colours::line);
    setColour (juce::ComboBox::arrowColourId, colours::sub);
    setColour (juce::AlertWindow::backgroundColourId, colours::body);
    setColour (juce::AlertWindow::textColourId, colours::text);
    setColour (juce::AlertWindow::outlineColourId, colours::line);
    setColour (juce::TextEditor::backgroundColourId, colours::well);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::outlineColourId, colours::line);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour (juce::TextEditor::highlightColourId, colours::accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, colours::text);
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::buttonColourId, colours::body);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, colours::line);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const float r = juce::jmin (w, h) * 0.5f - 1.0f;
    const float cx = x + w * 0.5f, cy = y + h * 0.5f;
    const float ringR = r - 2.5f;
    const juce::Colour arcColour = s.getProperties().contains ("arc") ? juce::Colour ((juce::uint32) (juce::int64) s.getProperties()["arc"]) : colours::accent;

    juce::Path track;
    track.addCentredArc (cx, cy, ringR, ringR, 0.0f, start, end, true);
    g.setColour (colours::well);
    g.strokePath (track, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    const float angle = start + pos * (end - start);
    const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (cx, cy, ringR, ringR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (s.isEnabled() ? arcColour : colours::faint);
        g.strokePath (value, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }
    if (bipolar)
    {
        g.setColour (colours::faint);
        g.fillRect (cx - 0.75f, cy - r - 1.0f, 1.5f, 4.0f);
    }

    // a bone cap with a moulded edge, as on the old machines
    const float capR = r - 8.0f;
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillEllipse (cx - capR, cy - capR + 3.0f, capR * 2, capR * 2);
    juce::ColourGradient cap (juce::Colour (0xfff3ecdb), cx - capR * 0.4f, cy - capR * 0.6f,
                              juce::Colour (0xffb9b19d), cx + capR * 0.7f, cy + capR, true);
    g.setGradientFill (cap);
    g.fillEllipse (cx - capR, cy - capR, capR * 2, capR * 2);
    g.setColour (juce::Colour (0xff8f8876));
    g.drawEllipse (cx - capR + 0.5f, cy - capR + 0.5f, capR * 2 - 1.0f, capR * 2 - 1.0f, 1.0f);
    const float inner = capR * 0.62f;
    g.setColour (juce::Colour (0xffe6dfcc));
    g.fillEllipse (cx - inner, cy - inner, inner * 2, inner * 2);

    const float sx = std::sin (angle), sy = -std::cos (angle);
    g.setColour (s.isEnabled() ? colours::ink : colours::faint);
    g.drawLine (cx + sx * capR * 0.25f, cy + sy * capR * 0.25f, cx + sx * capR * 0.9f, cy + sy * capR * 0.9f, 2.6f);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const juce::Colour lit = b.getProperties().contains ("lit") ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["lit"]) : colours::accent;
    if (on)
    {
        g.setColour (down ? lit.darker (0.15f) : lit);
        g.fillRoundedRectangle (r, 6.0f);
        return;
    }
    g.setColour (down ? colours::well.brighter (0.06f) : colours::well);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (over && b.isEnabled() ? colours::sub : colours::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    g.setColour (! b.isEnabled() ? colours::faint : (on ? colours::ink : colours::text));
    g.setFont (semi (b.getProperties().getWithDefault ("small", false) ? 12.0f : 13.0f));
    auto area = b.getLocalBounds().reduced (6, 0);
    if (b.getProperties().getWithDefault ("chevron", false))
    {
        auto arrow = area.removeFromRight (12).toFloat();
        juce::Path p;
        const float ax = arrow.getCentreX(), ay = arrow.getCentreY();
        p.startNewSubPath (ax - 4.0f, ay - 2.0f);
        p.lineTo (ax, ay + 2.0f);
        p.lineTo (ax + 4.0f, ay - 2.0f);
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.drawText (b.getButtonText(), area.withTrimmedLeft (4), juce::Justification::centredLeft, true);
        return;
    }
    g.drawText (b.getButtonText(), area, juce::Justification::centred, true);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (colours::well);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.isMouseOver (true) ? colours::sub : colours::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    juce::Path p;
    const float ax = (float) w - 13.0f, ay = h * 0.5f;
    p.startNewSubPath (ax - 4.0f, ay - 2.0f);
    p.lineTo (ax, ay + 2.0f);
    p.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (colours::sub);
    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (colours::lcd);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::lcdEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (colours::ink);
    g.setFont (sans (13.0f));
    g.drawFittedText (text, juce::Rectangle<int> (0, 0, w, h).reduced (10, 6), juce::Justification::centredLeft, 5);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const float textWidth = juce::GlyphArrangement::getStringWidth (sans (13.0f), text) + 24.0f;
    const int width = juce::jmin (360, (int) textWidth);
    const int lines = (int) std::ceil (textWidth / 340.0f);
    const int height = 14 + 17 * juce::jmax (1, lines);
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (width + 12) : pos.x + 18,
                                 pos.y > parent.getCentreY() ? pos.y - (height + 6) : pos.y + 18, width, height)
        .constrainedWithin (parent);
}

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                 int thumbStart, int thumbSize, bool over, bool down)
{
    auto thumb = vertical ? juce::Rectangle<float> ((float) x + w * 0.5f - 2.0f, (float) thumbStart, 4.0f, (float) thumbSize)
                          : juce::Rectangle<float> ((float) thumbStart, (float) y + h * 0.5f - 2.0f, (float) thumbSize, 4.0f);
    g.setColour (over || down ? colours::sub : colours::line);
    g.fillRoundedRectangle (thumb, 2.0f);
}

//==============================================================================
Knob::Knob (const FloatSpec& s) : spec (s)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setRotaryParameters (pi * 1.25f, pi * 2.75f, true);
    slider.setMouseDragSensitivity (240);
    slider.setScrollWheelEnabled (true);
    slider.getProperties().set ("bipolar", spec.bipolar);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void Knob::attach (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId)
{
    attachment.reset();
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramId, slider);
    slider.setDoubleClickReturnValue (true, spec.def);
    repaint();
}

void Knob::setLanguage (Lang l)
{
    lang = l;
    const juce::String note = u8 (lang == Lang::es ? spec.noteEs : spec.noteEn);
    slider.setTooltip ((note.isEmpty() ? juce::String() : note + ". ") + tr (lang, "dblclick") + ".");
    repaint();
}

void Knob::resized() { slider.setBounds ((width - dial) / 2, 0, dial, dial); }

void Knob::paint (juce::Graphics& g)
{
    const float alpha = isEnabled() ? 1.0f : 0.45f;
    g.setColour (colours::sub.withMultipliedAlpha (alpha));
    g.setFont (semi (12.5f));
    g.drawText (u8 (lang == Lang::es ? spec.es : spec.en), juce::Rectangle<float> (0.0f, (float) dial + 3.0f, (float) width, 16.0f),
                juce::Justification::centred, false);
    g.setColour (colours::text.withMultipliedAlpha (alpha));
    g.setFont (mono (12.5f));
    const float v = (float) slider.getValue();
    g.drawText (localValue (formatValue (spec, v), lang), juce::Rectangle<float> (0.0f, (float) dial + 19.0f, (float) width, 16.0f),
                juce::Justification::centred, false);
    if (extraText)
    {
        g.setColour (colours::faint.withMultipliedAlpha (alpha));
        g.setFont (mono (11.0f));
        g.drawText (extraText (v), juce::Rectangle<float> (0.0f, (float) dial + 35.0f, (float) width, 14.0f), juce::Justification::centred, false);
    }
}

//==============================================================================
void DragValue::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawWell (g, r);
    const float v = get ? get() : 0.0f;
    auto inner = r.reduced (9.0f, 0.0f);
    if (label.isNotEmpty())
    {
        g.setColour (colours::sub);
        g.setFont (semi (12.0f));
        g.drawText (label, inner, juce::Justification::centredLeft, false);
    }
    g.setColour (isEnabled() ? colours::text : colours::faint);
    g.setFont (mono (13.0f));
    g.drawText (text ? text (v) : juce::String (v), inner, label.isNotEmpty() ? juce::Justification::centredRight : juce::Justification::centred, false);
}

void DragValue::mouseDown (const juce::MouseEvent&)
{
    startValue = get ? get() : 0.0f;
    if (gesture) gesture (true);
}

void DragValue::mouseDrag (const juce::MouseEvent& e)
{
    const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
    float v = startValue - (float) e.getDistanceFromDragStartY() / pixelsPerUnit * fine;
    if (step > 0.0f) v = std::round (v / step) * step;
    if (set) set (juce::jlimit (min, max, v));
    repaint();
}

void DragValue::mouseUp (const juce::MouseEvent&) { if (gesture) gesture (false); }

void DragValue::mouseDoubleClick (const juce::MouseEvent&)
{
    if (gesture) gesture (true);
    if (set) set (def);
    if (gesture) gesture (false);
    repaint();
}

void DragValue::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float units = step > 0.0f ? step * (w.deltaY > 0 ? 1.0f : -1.0f) : (max - min) * 0.02f * (w.deltaY > 0 ? 1.0f : -1.0f);
    if (gesture) gesture (true);
    if (set && get) set (juce::jlimit (min, max, get() + units));
    if (gesture) gesture (false);
    repaint();
}

//==============================================================================
Segmented::Segmented (std::vector<juce::String> l, std::function<int()> g, std::function<void (int)> s)
    : labels (std::move (l)), get (std::move (g)), set (std::move (s))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    const int n = (int) labels.size();
    const int i = juce::jlimit (0, n - 1, (int) (e.position.x / (float) getWidth() * n));
    if (isEnabled() && set) set (i);
    repaint();
}

void Segmented::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const int n = (int) labels.size();
    const int current = get ? get() : 0;
    juce::Path clip;
    clip.addRoundedRectangle (r, 6.0f);
    g.saveState();
    g.reduceClipRegion (clip);
    g.setColour (colours::well);
    g.fillRect (r);
    for (int i = 0; i < n; ++i)
    {
        auto c = juce::Rectangle<float> (r.getX() + r.getWidth() * i / n, r.getY(), r.getWidth() / n, r.getHeight());
        const bool on = i == current;
        if (on) { g.setColour (isEnabled() ? onColour : colours::faint); g.fillRect (c); }
        else if (i > 0) { g.setColour (colours::line); g.fillRect (c.getX(), c.getY(), 1.0f, c.getHeight()); }
        g.setColour (on ? colours::ink : (isEnabled() ? colours::sub : colours::faint));
        g.setFont (semi (12.5f));
        g.drawText (labels[(size_t) i], c, juce::Justification::centred);
    }
    g.restoreState();
    g.setColour (colours::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

//==============================================================================
PadGrid::PadGrid (KupataProcessor& p) : processor (p) {}

juce::Rectangle<float> PadGrid::cell (int pad) const
{
    constexpr float gap = 12.0f;
    const float size = ((float) getWidth() - gap * 3.0f) / 4.0f;
    const int col = pad % 4, row = 3 - pad / 4;   // pad 1 at the bottom left
    return { col * (size + gap), row * (size + gap), size, size };
}

int PadGrid::padAt (juce::Point<float> p) const
{
    for (int i = 0; i < numPads; ++i) if (cell (i).contains (p)) return i;
    return -1;
}

const std::vector<float>& PadGrid::miniWave (int pad, const KupataProcessor::PadInfo& info)
{
    static const std::vector<float> none;
    const Sound* s = info.sound.get();
    if (s == nullptr) return none;
    auto it = mini.find (s);
    if (it != mini.end()) return it->second;
    std::vector<float> w (48, 0.0f);
    const auto& smp = *s->sample;
    const int len = s->regionLength();
    for (int c = 0; c < 48; ++c)
    {
        const int a = s->regionStart + (int) ((juce::int64) len * c / 48), b = s->regionStart + (int) ((juce::int64) len * (c + 1) / 48);
        float m = 0.0f;
        for (int i = a; i < b; i += 4) m = juce::jmax (m, std::abs (smp.left[(size_t) i]), std::abs (smp.right[(size_t) i]));
        w[(size_t) c] = m / s->peak;
    }
    juce::ignoreUnused (pad);
    return mini.emplace (s, std::move (w)).first->second;
}

void PadGrid::update (const Telemetry& t, Lang l)
{
    tele = t;
    lang = l;
    repaint();
}

void PadGrid::paint (juce::Graphics& g)
{
    const int selected = processor.getSelectedPad();
    const int learning = processor.learningPad();
    bool anySolo = false;
    for (int q = 0; q < numPads; ++q) anySolo = anySolo || processor.padParams (q).solo;
    for (int pad = 0; pad < numPads; ++pad)
    {
        const auto info = processor.padInfo (pad);
        const auto params = processor.padParams (pad);
        auto r = cell (pad);
        const juce::Colour rowColour = colours::row (pad);
        const float level = juce::jlimit (0.0f, 1.0f, tele.padLevel[pad] * 1.6f);

        // the recess, then the rubber pad, lit from inside by its row colour when it sounds
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 11.0f);
        g.setColour (colours::well);
        g.fillRoundedRectangle (r, 11.0f);
        auto face = r.reduced (3.0f);
        juce::ColourGradient rubber (juce::Colour (0xff45433d).interpolatedWith (rowColour, 0.25f * level + (pressed == pad ? 0.12f : 0.0f)), face.getX(), face.getY(),
                                     juce::Colour (0xff2f2e2a).interpolatedWith (rowColour, 0.18f * level), face.getX(), face.getBottom(), false);
        g.setGradientFill (rubber);
        g.fillRoundedRectangle (face, 9.0f);

        // the colour strip at the bottom
        auto strip = face.removeFromBottom (7.0f).reduced (8.0f, 0.0f).translated (0.0f, -5.0f);
        g.setColour (rowColour.withAlpha (info.loaded() ? 0.45f + 0.55f * level : 0.18f));
        g.fillRoundedRectangle (strip, 2.5f);

        auto inner = r.reduced (11.0f, 9.0f);
        auto top = inner.removeFromTop (14.0f);
        g.setFont (mono (11.0f));
        g.setColour (colours::faint);
        g.drawText (juce::String (pad + 1), top, juce::Justification::centredLeft, false);
        g.setColour (learning == pad ? colours::accent : colours::faint);
        g.drawText (learning == pad ? juce::String ("? ?") : noteName (info.routing.note, lang), top, juce::Justification::centredRight, false);

        // badges: mute, solo, choke
        juce::String badges;
        if (params.mute) badges << "M ";
        if (params.solo) badges << "S ";
        if (info.routing.choke > 0) badges << (lang == Lang::es ? "C" : "C") << info.routing.choke;
        if (badges.isNotEmpty())
        {
            g.setColour (params.solo ? colours::accent : colours::sub);
            g.setFont (monoBold (10.5f));
            g.drawText (badges.trim(), top.translated (0.0f, 13.0f), juce::Justification::centredRight, false);
        }

        // the name, and a small picture of the sound
        auto nameArea = inner.removeFromTop (inner.getHeight() * 0.55f);
        const bool dim = params.mute || (anySolo && ! params.solo);
        if (info.loaded())
        {
            g.setColour (dim ? colours::faint : colours::text);
            g.setFont (semi (14.0f));
            g.drawFittedText (info.name, nameArea.toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);
            const auto& w = miniWave (pad, info);
            auto wa = inner.reduced (0.0f, 4.0f).withTrimmedBottom (6.0f);
            juce::Path p;
            const float mid = wa.getCentreY(), half = wa.getHeight() * 0.5f;
            const float colW = wa.getWidth() / (float) w.size();
            for (size_t c = 0; c < w.size(); ++c)
            {
                const float h = juce::jmax (0.6f, w[c] * half);
                p.addRectangle (wa.getX() + c * colW, mid - h, juce::jmax (1.0f, colW - 1.0f), h * 2.0f);
            }
            g.setColour ((dim ? colours::faint : colours::sub).withAlpha (0.55f));
            g.fillPath (p);
            if (tele.padPos[pad] >= 0.0f)
            {
                g.setColour (rowColour.withAlpha (0.9f));
                g.fillRect (wa.getX() + wa.getWidth() * tele.padPos[pad] - 0.75f, wa.getY(), 1.5f, wa.getHeight());
            }
        }
        else
        {
            g.setColour (colours::faint);
            g.setFont (semi (13.0f));
            g.drawFittedText (info.missing ? tr (lang, "missing") : tr (lang, "empty"), nameArea.toNearestInt(), juce::Justification::centredLeft, 2);
            if (info.missing)
            {
                g.setFont (mono (10.5f));
                g.drawFittedText (juce::File (info.path).getFileName(), inner.toNearestInt(), juce::Justification::topLeft, 2);
            }
        }

        if (pad == selected)
        {
            g.setColour (colours::text);
            g.drawRoundedRectangle (r.reduced (0.5f), 11.0f, 2.0f);
        }
        if (pad == dropTarget)
        {
            g.setColour (colours::accent);
            g.drawRoundedRectangle (r.reduced (1.0f), 11.0f, 2.5f);
        }
        else if (pad == hover && pad != selected)
        {
            g.setColour (colours::line.brighter (0.2f));
            g.drawRoundedRectangle (r.reduced (0.5f), 11.0f, 1.0f);
        }
    }
}

void PadGrid::mouseMove (const juce::MouseEvent& e)
{
    const int p = padAt (e.position);
    if (p != hover) { hover = p; repaint(); }
    if (p >= 0) setTooltip (tr (lang, processor.padInfo (p).loaded() ? "padTip" : "emptyPadTip"));
}

void PadGrid::mouseDown (const juce::MouseEvent& e)
{
    const int p = padAt (e.position);
    if (p < 0) return;
    dragging = false;
    if (onSelect) onSelect (p);
    if (e.mods.isPopupMenu()) { if (onMenu) onMenu (p); return; }
    pressed = p;
    // the lower part of a pad plays louder, as on a real one
    const auto r = cell (p);
    const float vel = juce::jlimit (0.3f, 1.0f, 0.45f + 0.6f * (e.position.y - r.getY()) / r.getHeight());
    processor.playPad (p, vel);
    repaint();
}

void PadGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed < 0 || dragging || e.getDistanceFromDragStart() < 8) return;
    if (! processor.padInfo (pressed).loaded()) return;
    if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        processor.releasePad (pressed);
        auto img = createComponentSnapshot (cell (pressed).toNearestInt(), true, 1.0f);
        c->startDragging ("pad:" + juce::String (pressed), this, juce::ScaledImage (img));
    }
}

void PadGrid::mouseUp (const juce::MouseEvent&)
{
    if (pressed >= 0 && ! dragging) processor.releasePad (pressed);
    pressed = -1;
    dragging = false;
    repaint();
}

bool PadGrid::isInterestedInDragSource (const SourceDetails& d)
{
    const auto s = d.description.toString();
    return s.startsWith ("pad:") || s.startsWith ("file:");
}

void PadGrid::itemDragMove (const SourceDetails& d)
{
    const int p = padAt (d.localPosition.toFloat());
    if (p != dropTarget) { dropTarget = p; repaint(); }
}

void PadGrid::itemDropped (const SourceDetails& d)
{
    const int p = padAt (d.localPosition.toFloat());
    dropTarget = -1;
    const auto s = d.description.toString();
    if (p >= 0 && s.startsWith ("pad:") && onMove)
        onMove (s.fromFirstOccurrenceOf (":", false, false).getIntValue(), p, juce::ModifierKeys::getCurrentModifiers().isAltDown());
    if (p >= 0 && s.startsWith ("file:") && onFiles)
        onFiles (p, juce::StringArray (s.fromFirstOccurrenceOf (":", false, false)));
    repaint();
}

void PadGrid::fileDragMove (const juce::StringArray&, int x, int y)
{
    const int p = padAt ({ (float) x, (float) y });
    if (p != dropTarget) { dropTarget = p; repaint(); }
}

void PadGrid::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int p = padAt ({ (float) x, (float) y });
    dropTarget = -1;
    if (p >= 0 && onFiles) onFiles (p, files);
    repaint();
}

//==============================================================================
Screen::Screen (KupataProcessor& p) : processor (p) {}

juce::Rectangle<float> Screen::waveArea() const
{
    return getLocalBounds().toFloat().reduced (14.0f, 0.0f).withTrimmedTop (62.0f).withTrimmedBottom (26.0f);
}

void Screen::setPad (int p, const KupataProcessor::PadInfo& i, const PadParams& pp, Lang l)
{
    const bool soundChanged = i.sound != info.sound || p != pad;
    pad = p;
    info = i;
    lang = l;
    auto moved = [] (float a, float b) { return std::abs (a - b) > 1.0e-5f; };
    const bool paramsChanged = moved (pp.shape, params.shape) || moved (pp.tune, params.tune) || moved (pp.tone, params.tone)
                               || moved (pp.comp, params.comp) || moved (pp.start, params.start) || moved (pp.end, params.end);
    params = pp;
    if (soundChanged || paramsChanged || ! renderedValid)
    {
        if (info.sound != nullptr)
        {
            const int maxFrames = (int) (info.sound->sample->sampleRate * 8.0);
            processor.renderPicture (pad, rendered, maxFrames);
            renderedSeconds = rendered.size() / info.sound->sample->sampleRate;
        }
        else rendered.clear();
        renderedValid = true;
        peaksFor = nullptr;
    }
    setTooltip (info.loaded() ? tr (lang, "waveTip") : tr (lang, "emptyPadTip"));
    setMouseCursor (info.loaded() ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    repaint();
}

void Screen::computePeaks()
{
    const auto area = waveArea();
    const int cols = juce::jmax (1, (int) area.getWidth());
    peaksOriginal.assign ((size_t) cols, 0.0f);
    peaksRendered.assign ((size_t) cols, 0.0f);
    peaksFor = info.sound.get();
    if (info.sound == nullptr) return;
    const auto& s = *info.sound;
    const auto& smp = *s.sample;
    const double sr = smp.sampleRate;
    const double regionSec = s.regionLength() / sr;
    const double startSec = params.start * 0.01 * regionSec;
    const double total = juce::jmax (regionSec, startSec + renderedSeconds);
    const double framesPerCol = total * sr / cols;
    for (int c = 0; c < cols; ++c)
    {
        const double a = c * framesPerCol, b = (c + 1) * framesPerCol;
        float m = 0.0f;
        for (int i = (int) a; i < (int) b; ++i)
        {
            const int k = s.regionStart + i;
            if (k >= s.regionEnd) break;
            m = juce::jmax (m, std::abs (smp.left[(size_t) k]), std::abs (smp.right[(size_t) k]));
        }
        peaksOriginal[(size_t) c] = m / s.peak;
        float q = 0.0f;
        const double off = startSec * sr;
        for (int i = (int) (a - off); i < (int) (b - off); ++i)
            if (i >= 0 && i < (int) rendered.size()) q = juce::jmax (q, std::abs (rendered[(size_t) i]));
        peaksRendered[(size_t) c] = q / s.peak;
    }
}

void Screen::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    // the glass, set into the case
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r, 10.0f);
    auto glass = r.reduced (3.0f);
    juce::ColourGradient lcd (colours::lcd.brighter (0.04f), glass.getX(), glass.getY(), colours::lcd.darker (0.06f), glass.getX(), glass.getBottom(), false);
    g.setGradientFill (lcd);
    g.fillRoundedRectangle (glass, 8.0f);
    g.setColour (juce::Colours::black.withAlpha (0.12f));
    g.drawRoundedRectangle (glass.reduced (0.5f), 8.0f, 1.5f);

    auto text = glass.reduced (12.0f, 9.0f);
    auto line1 = text.removeFromTop (22.0f);
    auto line2 = text.removeFromTop (18.0f);
    g.setColour (colours::ink);
    g.setFont (semi (17.0f));
    g.drawText (info.loaded() || info.missing ? info.name : tr (lang, "empty"), line1.withTrimmedRight (120.0f), juce::Justification::centredLeft, true);
    g.setFont (mono (12.0f));
    g.drawText ("Pad " + juce::String (pad + 1), line1, juce::Justification::centredRight, false);

    if (! info.loaded())
    {
        g.setColour (colours::inkDim);
        g.setFont (sans (14.0f));
        g.drawText (info.missing ? tr (lang, "missing") + ": " + info.path : tr (lang, "emptyPadTip"), waveArea(), juce::Justification::centred, true);
        return;
    }

    const auto& s = *info.sound;
    const double sr = s.sample->sampleRate;
    g.setColour (colours::inkDim);
    g.setFont (mono (11.5f));
    juce::String detail = secondsText (s.regionLength() / sr, lang) + "   " + tr (lang, "pitchOf") + " ";
    detail << (s.pitch > 0.0f ? pitchName (s.pitch * std::pow (2.0f, params.tune / 12.0f), lang) : tr (lang, "noPitch"));
    g.drawText (detail, line2, juce::Justification::centredLeft, false);

    if (peaksFor != info.sound.get() || (int) peaksOriginal.size() != (int) waveArea().getWidth()) computePeaks();
    const auto area = waveArea();
    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f - 2.0f;
    const int cols = (int) peaksOriginal.size();

    // the baseline, the sound as it is (light), and as it sounds now (ink)
    g.setColour (colours::inkDim.withAlpha (0.5f));
    g.fillRect (area.getX(), mid - 0.5f, area.getWidth(), 1.0f);
    juce::Path orig, now;
    for (int c = 0; c < cols; ++c)
    {
        const float x = area.getX() + (float) c;
        const float h0 = juce::jmin (1.0f, peaksOriginal[(size_t) c]) * half;
        if (h0 > 0.3f) orig.addRectangle (x, mid - h0, 1.0f, h0 * 2.0f);
        const float h1 = juce::jmin (1.15f, peaksRendered[(size_t) c]) * half;
        if (h1 > 0.3f) now.addRectangle (x, mid - h1, 1.0f, h1 * 2.0f);
    }
    g.setColour (colours::inkDim.withAlpha (0.45f));
    g.fillPath (orig);
    g.setColour (colours::ink);
    g.fillPath (now);

    // trim markers, measured on the sound itself
    const double regionSec = s.regionLength() / sr;
    const double total = juce::jmax (regionSec, params.start * 0.01 * regionSec + renderedSeconds);
    auto xForPercent = [&] (float pct) { return area.getX() + (float) (pct * 0.01 * regionSec / total) * area.getWidth(); };
    const float xs = xForPercent (params.start), xe = xForPercent (params.end);
    g.setColour (colours::lcd.withAlpha (0.6f));
    if (xs > area.getX()) g.fillRect (area.getX(), area.getY(), xs - area.getX(), area.getHeight());
    if (xe < area.getRight()) g.fillRect (xe, area.getY(), area.getRight() - xe, area.getHeight());
    for (int m = 0; m < 2; ++m)
    {
        const float x = m == 0 ? xs : xe;
        g.setColour (m == hoverMarker || m == dragMarker ? colours::row (pad).darker (0.3f) : colours::ink);
        g.fillRect (x - 0.75f, area.getY() - 6.0f, 1.5f, area.getHeight() + 6.0f);
        juce::Path flag;
        const float dir = m == 0 ? 1.0f : -1.0f;
        flag.addTriangle (x, area.getY() - 8.0f, x + 9.0f * dir, area.getY() - 8.0f, x, area.getY() + 1.0f);
        g.fillPath (flag);
    }
    g.setColour (colours::inkDim);
    g.setFont (mono (11.0f));
    auto foot = juce::Rectangle<float> (area.getX(), area.getBottom() + 5.0f, area.getWidth(), 16.0f);
    g.drawText (u8 (lang == Lang::es ? "Inicio " : "Start ") + localNumber (juce::String (params.start, 1), lang) + " %",
                foot, juce::Justification::centredLeft, false);
    g.drawText (u8 (lang == Lang::es ? "Final " : "End ") + localNumber (juce::String (params.end, 1), lang) + " %",
                foot, juce::Justification::centredRight, false);

    // where the newest hit is playing
    const float pos = tele.padPos[pad];
    if (pos >= 0.0f)
    {
        const float x = area.getX() + (float) (pos * regionSec / total) * area.getWidth();
        g.setColour (colours::row (pad).darker (0.35f));
        g.fillRect (x - 1.0f, area.getY(), 2.0f, area.getHeight());
    }
}

int Screen::markerAt (juce::Point<float> p) const
{
    if (info.sound == nullptr) return -1;
    const auto area = waveArea();
    const double sr = info.sound->sample->sampleRate;
    const double regionSec = info.sound->regionLength() / sr;
    const double total = juce::jmax (regionSec, params.start * 0.01 * regionSec + renderedSeconds);
    auto xFor = [&] (float pct) { return area.getX() + (float) (pct * 0.01 * regionSec / total) * area.getWidth(); };
    if (p.y < area.getY() - 12.0f || p.y > area.getBottom()) return -1;
    const float ds = std::abs (p.x - xFor (params.start)), de = std::abs (p.x - xFor (params.end));
    if (juce::jmin (ds, de) > 7.0f) return -1;
    return ds <= de ? 0 : 1;
}

void Screen::mouseMove (const juce::MouseEvent& e)
{
    const int m = markerAt (e.position);
    if (m != hoverMarker) { hoverMarker = m; repaint(); }
    setMouseCursor (m >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                           : (info.loaded() ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor));
}

void Screen::mouseDown (const juce::MouseEvent& e)
{
    if (! info.loaded()) { if (onLoadRequest) onLoadRequest(); return; }
    dragMarker = markerAt (e.position);
    if (dragMarker >= 0)
        if (auto* param = processor.apvts.getParameter (padId (pad, dragMarker == 0 ? "start" : "end"))) param->beginChangeGesture();
}

void Screen::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMarker < 0 || info.sound == nullptr) return;
    const auto area = waveArea();
    const double sr = info.sound->sample->sampleRate;
    const double regionSec = info.sound->regionLength() / sr;
    const double total = juce::jmax (regionSec, params.start * 0.01 * regionSec + renderedSeconds);
    float pct = (float) ((e.position.x - area.getX()) / area.getWidth() * total / regionSec * 100.0);
    pct = dragMarker == 0 ? juce::jlimit (0.0f, params.end - 0.5f, pct) : juce::jlimit (params.start + 0.5f, 100.0f, pct);
    if (auto* param = processor.apvts.getParameter (padId (pad, dragMarker == 0 ? "start" : "end")))
        param->setValueNotifyingHost (param->convertTo0to1 (pct));
}

void Screen::mouseUp (const juce::MouseEvent&)
{
    if (dragMarker >= 0)
        if (auto* param = processor.apvts.getParameter (padId (pad, dragMarker == 0 ? "start" : "end"))) param->endChangeGesture();
    dragMarker = -1;
}

//==============================================================================
HitBar::HitBar (KupataProcessor& p) : processor (p)
{
    prev.setButtonText (u8 ("‹"));
    next.setButtonText (u8 ("›"));
    for (auto* b : { &prev, &next, &whole, &toPads }) { addAndMakeVisible (*b); b->getProperties().set ("small", true); }
    prev.onClick = [this] { processor.setHit (pad, hit <= 1 ? numHits : hit - 1); if (onChange) onChange(); };
    next.onClick = [this] { processor.setHit (pad, hit >= numHits ? 1 : hit + 1); if (onChange) onChange(); };
    whole.onClick = [this] { processor.setHit (pad, hit == 0 ? 1 : 0); if (onChange) onChange(); };
    toPads.onClick = [this] { processor.hitsToPads (pad); if (onChange) onChange(); };
}

void HitBar::resized()
{
    auto r = getLocalBounds();
    toPads.setBounds (r.removeFromRight (120));
    r.removeFromRight (8);
    whole.setBounds (r.removeFromRight (74));
    r.removeFromRight (8);
    prev.setBounds (r.removeFromLeft (30));
    next.setBounds (r.removeFromRight (30));
}

void HitBar::setPad (int p, const KupataProcessor::PadInfo& info, Lang l)
{
    pad = p; numHits = info.numHits; hit = info.hit; lang = l;
    whole.setButtonText (tr (lang, "whole"));
    whole.setToggleState (hit == 0, juce::dontSendNotification);
    toPads.setButtonText (tr (lang, "sliceToPads"));
    toPads.setTooltip (tr (lang, "sliceTip"));
    setTooltip (tr (lang, "hitTip"));
    whole.setTooltip (tr (lang, "hitTip"));
    setVisible (info.loaded() && numHits > 1);
    repaint();
}

void HitBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().withTrimmedLeft (34.0f).withTrimmedRight (34.0f + 74.0f + 8.0f + 120.0f + 8.0f);
    drawWell (g, r);
    g.setColour (hit == 0 ? colours::sub : colours::text);
    g.setFont (mono (12.5f));
    const juce::String t = hit == 0 ? juce::String (numHits) + " " + tr (lang, "hits")
                                    : tr (lang, "hit") + " " + juce::String (hit) + " / " + juce::String (numHits);
    g.drawText (t, r, juce::Justification::centred, false);
}

//==============================================================================
Browser::Browser (KupataProcessor& p) : processor (p)
{
    list.setRowHeight (22);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setMultipleSelectionEnabled (false);
    addAndMakeVisible (list);
    for (auto* b : { &up, &choose }) { addAndMakeVisible (*b); b->getProperties().set ("small", true); }
    up.onClick = [this] { if (folder.getParentDirectory() != folder) setFolder (folder.getParentDirectory()); };
    choose.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> (tr (lang, "chooseFolder"), folder);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().isDirectory()) setFolder (fc.getResult()); });
    };
    juce::File start (processor.apvts.state.getProperty ("browser", "").toString());
    if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    setFolder (start);
}

void Browser::setLanguage (Lang l)
{
    lang = l;
    up.setButtonText (tr (lang, "up"));
    choose.setButtonText (tr (lang, "chooseFolder"));
    list.setTooltip (tr (lang, "browserTip"));
    repaint();
}

void Browser::setFolder (const juce::File& f)
{
    folder = f;
    processor.apvts.state.setProperty ("browser", f.getFullPathName(), nullptr);
    items.clear();
    juce::Array<juce::File> dirs, files;
    for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
    {
        const auto file = entry.getFile();
        if (entry.isDirectory()) dirs.add (file);
        else if (isAudioFile (file, processor.formats)) files.add (file);
    }
    auto byName = [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; };
    std::sort (dirs.begin(), dirs.end(), byName);
    std::sort (files.begin(), files.end(), byName);
    items.addArray (dirs);
    items.addArray (files);
    list.updateContent();
    list.deselectAllRows();
    list.setVerticalPosition (0.0);
    repaint();
}

void Browser::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (44);
    auto buttons = r.removeFromBottom (30);
    up.setBounds (buttons.removeFromLeft (70));
    buttons.removeFromLeft (8);
    choose.setBounds (buttons);
    r.removeFromBottom (8);
    list.setBounds (r.reduced (2, 4));
}

void Browser::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::text);
    g.setFont (semi (14.0f));
    g.drawText (tr (lang, "browser"), r.removeFromTop (20.0f), juce::Justification::centredLeft, false);
    g.setColour (colours::faint);
    g.setFont (mono (11.0f));
    g.drawText (folder.getFileName().isEmpty() ? folder.getFullPathName() : folder.getFileName(), r.removeFromTop (20.0f), juce::Justification::centredLeft, true);
    r.removeFromTop (4.0f);
    r.removeFromBottom (38.0f);
    drawWell (g, r, 8.0f);
    if (items.isEmpty())
    {
        g.setColour (colours::faint);
        g.setFont (sans (13.0f));
        g.drawFittedText (tr (lang, "noSounds"), r.reduced (14.0f).toNearestInt(), juce::Justification::centred, 3);
    }
}

void Browser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, items.size())) return;
    const auto& f = items.getReference (row);
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (4.0f, 1.0f);
    if (selected)
    {
        g.setColour (colours::line);
        g.fillRoundedRectangle (r, 4.0f);
    }
    const bool dir = f.isDirectory();
    auto icon = r.removeFromLeft (18.0f);
    if (dir)
    {
        juce::Path folderIcon;
        auto b = icon.withSizeKeepingCentre (12.0f, 9.0f);
        folderIcon.addRoundedRectangle (b, 1.5f);
        folderIcon.addRectangle (b.getX(), b.getY() - 2.0f, 5.0f, 2.5f);
        g.setColour (colours::sub);
        g.fillPath (folderIcon);
    }
    else
    {
        g.setColour (colours::faint);
        const auto c = icon.getCentre();
        for (int k = 0; k < 4; ++k)
        {
            const float hh = (float) (2 + (k * 7) % 5) * 1.3f;
            g.fillRect (c.x - 5.0f + k * 3.0f, c.y - hh * 0.5f, 1.6f, hh);
        }
    }
    g.setColour (dir ? colours::text : colours::sub);
    if (selected) g.setColour (colours::text);
    g.setFont (sans (13.0f));
    g.drawText (dir ? f.getFileName() : f.getFileNameWithoutExtension(), r.withTrimmedLeft (4.0f), juce::Justification::centredLeft, true);
}

void Browser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, items.size()) || e.mods.isPopupMenu()) return;
    const auto f = items[row];
    if (! f.isDirectory()) processor.previewFile (f);
}

void Browser::selectedRowsChanged (int row)
{
    // moving through the list with the arrow keys listens to each sound
    if (juce::isPositiveAndBelow (row, items.size()) && ! items[row].isDirectory()
        && (juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::upKey) || juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::downKey)))
        processor.previewFile (items[row]);
}

void Browser::open (int row)
{
    if (! juce::isPositiveAndBelow (row, items.size())) return;
    const auto f = items[row];
    if (f.isDirectory()) { setFolder (f); return; }
    processor.stopPreview();
    if (onLoad) onLoad (f);
}

juce::var Browser::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.isEmpty()) return {};
    const auto f = items[rows[0]];
    if (f.isDirectory()) return {};
    return "file:" + f.getFullPathName();
}

juce::String Browser::getTooltipForRow (int) { return tr (lang, "browserTip"); }

//==============================================================================
Content::Content (KupataProcessor& p)
    : processor (p), browser (p), pads (p), screen (p), hitBar (p),
      stereoSwitch ({ "M", "ST" },
                    [this] { return processor.padInfo (processor.getSelectedPad()).routing.stereo ? 1 : 0; },
                    [this] (int i) {
                        const int pad = processor.getSelectedPad();
                        auto r = processor.padInfo (pad).routing;
                        r.stereo = i == 1;
                        if (r.stereo) r.out = juce::jmin (r.out, juce::jmax (0, maxOutputs - 2));
                        processor.setRouting (pad, r);
                        fillOutputs();
                    })
{
    setLookAndFeel (&lnf);
    lang = processor.getLanguage();

    for (auto* b : { &langEn, &langEs })
    {
        addAndMakeVisible (*b);
        b->setClickingTogglesState (false);
        b->getProperties().set ("small", true);
    }
    langEn.onClick = [this] { setLanguage (Lang::en); };
    langEs.onClick = [this] { setLanguage (Lang::es); };

    addAndMakeVisible (kitButton);
    kitButton.getProperties().set ("chevron", true);
    kitButton.onClick = [this] { showKitMenu(); };
    addAndMakeVisible (notesButton);
    notesButton.getProperties().set ("chevron", true);
    notesButton.onClick = [this] { showNotesMenu(); };
    addAndMakeVisible (multiOutButton);
    multiOutButton.onClick = [this] {
        juce::PopupMenu m;
        m.addItem (1, tr (lang, "multiOut"), processor.numOutputs() >= 4);
        m.addItem (2, tr (lang, "allTo12"));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&multiOutButton), [this] (int r) {
            if (r == 1) processor.setMultiOut (true);
            if (r == 2) processor.setMultiOut (false);
            fillOutputs();
        });
    };

    auto paramValue = [this] (const char* id) { return [this, id] { return processor.apvts.getRawParameterValue (id)->load(); }; };
    auto paramSet = [this] (const char* id) { return [this, id] (float v) {
        if (auto* prm = processor.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); }; };
    auto paramGesture = [this] (const char* id) { return [this, id] (bool begin) {
        if (auto* prm = processor.apvts.getParameter (id)) { if (begin) prm->beginChangeGesture(); else prm->endChangeGesture(); } }; };

    output.get = paramValue ("gain"); output.set = paramSet ("gain"); output.gesture = paramGesture ("gain");
    output.min = -36.0f; output.max = 12.0f; output.def = 0.0f; output.pixelsPerUnit = 4.0f;
    output.text = [this] (float v) { return localValue (formatValue (spec ("gain"), v), lang); };
    velocity.get = paramValue ("velocity"); velocity.set = paramSet ("velocity"); velocity.gesture = paramGesture ("velocity");
    velocity.min = 0.0f; velocity.max = 100.0f; velocity.def = 70.0f; velocity.pixelsPerUnit = 1.5f; velocity.step = 1.0f;
    velocity.text = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    addAndMakeVisible (output);
    addAndMakeVisible (velocity);

    addAndMakeVisible (browser);
    browser.onLoad = [this] (const juce::File& f) { loadFiles (processor.getSelectedPad(), juce::StringArray (f.getFullPathName())); };

    addAndMakeVisible (pads);
    pads.onSelect = [this] (int pad) { selectPad (pad); };
    pads.onMenu = [this] (int pad) { showPadMenu (pad); };
    pads.onFiles = [this] (int pad, const juce::StringArray& files) { loadFiles (pad, files); };
    pads.onMove = [this] (int from, int to, bool copy) { processor.movePad (from, to, copy); selectPad (to); pads.refresh(); };

    addAndMakeVisible (screen);
    screen.onLoadRequest = [this] { chooseFile (processor.getSelectedPad()); };
    addChildComponent (hitBar);
    hitBar.onChange = [this] { pads.refresh(); refreshPad (true); };

    const char* knobIds[] { "shape", "tune", "tone", "comp", "volume", "pan" };
    for (const char* id : knobIds)
    {
        auto* k = knobs.add (new Knob (spec (id)));
        addAndMakeVisible (k);
    }
    knobs[1]->extraText = [this] (float tune) {
        const auto info = processor.padInfo (processor.getSelectedPad());
        if (info.sound == nullptr || info.sound->pitch <= 0.0f) return juce::String();
        const float hz = info.sound->pitch * std::pow (2.0f, tune / 12.0f);
        return noteName (juce::roundToInt (69.0f + 12.0f * std::log2 (hz / 440.0f)), lang);
    };

    for (auto* b : { &gateButton, &muteButton, &soloButton })
    {
        addAndMakeVisible (*b);
        b->setClickingTogglesState (true);
    }
    soloButton.getProperties().set ("lit", (juce::int64) colours::row (8).getARGB());
    muteButton.getProperties().set ("lit", (juce::int64) colours::sub.getARGB());

    addAndMakeVisible (chokeBox);
    chokeBox.onChange = [this] {
        const int pad = processor.getSelectedPad();
        auto r = processor.padInfo (pad).routing;
        r.choke = juce::jmax (0, chokeBox.getSelectedId() - 1);
        processor.setRouting (pad, r);
        pads.repaint();
    };
    addAndMakeVisible (outBox);
    outBox.onChange = [this] {
        const int pad = processor.getSelectedPad();
        auto r = processor.padInfo (pad).routing;
        if (outBox.getSelectedId() > 0) r.out = outBox.getSelectedId() - 1;
        processor.setRouting (pad, r);
    };
    addAndMakeVisible (stereoSwitch);

    noteValue.get = [this] { return (float) processor.padInfo (processor.getSelectedPad()).routing.note; };
    noteValue.set = [this] (float v) {
        const int pad = processor.getSelectedPad();
        auto r = processor.padInfo (pad).routing;
        r.note = juce::roundToInt (v);
        processor.setRouting (pad, r);
        pads.repaint();
    };
    noteValue.min = 0.0f; noteValue.max = 127.0f; noteValue.def = 36.0f; noteValue.step = 1.0f; noteValue.pixelsPerUnit = 5.0f;
    noteValue.text = [this] (float v) { return noteName (juce::roundToInt (v), lang); };
    addAndMakeVisible (noteValue);
    addAndMakeVisible (learnButton);
    learnButton.getProperties().set ("small", true);
    learnButton.onClick = [this] {
        const int pad = processor.getSelectedPad();
        processor.startLearn (processor.learningPad() == pad ? -1 : pad);
    };

    setLanguage (lang);
    selectPad (processor.getSelectedPad());
    startTimerHz (30);
}

Content::~Content()
{
    stopTimer();
    processor.stopPreview();
    setLookAndFeel (nullptr);
}

void Content::setLanguage (Lang l)
{
    lang = l;
    processor.setLanguage (l);
    langEn.setToggleState (lang == Lang::en, juce::dontSendNotification);
    langEs.setToggleState (lang == Lang::es, juce::dontSendNotification);
    browser.setLanguage (lang);
    for (auto* k : knobs) k->setLanguage (lang);
    updateTexts();
    refreshPad (true);
    pads.refresh();
    repaint();
}

void Content::updateTexts()
{
    const auto name = processor.getKitName();
    kitButton.setButtonText (tr (lang, "kit") + ": " + (name.isNotEmpty() ? name : tr (lang, "factoryKit")));
    kitButton.setTooltip (tr (lang, "kitTip"));
    notesButton.setButtonText (tr (lang, "notes"));
    notesButton.setTooltip (tr (lang, "notesTip"));
    multiOutButton.setButtonText (tr (lang, "multiOut"));
    multiOutButton.setTooltip (tr (lang, "multiOutTip"));
    output.label = u8 (lang == Lang::es ? "Salida" : "Output");
    output.setTooltip (u8 (lang == Lang::es ? spec ("gain").noteEs : spec ("gain").noteEn));
    velocity.label = u8 (lang == Lang::es ? "Velocidad" : "Velocity");
    velocity.setTooltip (u8 (lang == Lang::es ? spec ("velocity").noteEs : spec ("velocity").noteEn));
    gateButton.setButtonText (tr (lang, "gate"));
    gateButton.setTooltip (tr (lang, "gateTip"));
    muteButton.setButtonText (tr (lang, "mute"));
    soloButton.setButtonText (tr (lang, "solo"));
    chokeBox.setTooltip (tr (lang, "chokeTip"));
    outBox.setTooltip (tr (lang, "outTip"));
    stereoSwitch.setLabels ({ tr (lang, "monoShort"), tr (lang, "stereoShort") });
    stereoSwitch.setTooltip (tr (lang, "stereoTip"));
    noteValue.setTooltip (tr (lang, "noteTip"));
    learnButton.setTooltip (tr (lang, "learn"));
    const int sel = chokeBox.getSelectedId();
    chokeBox.clear (juce::dontSendNotification);
    chokeBox.addItem (tr (lang, "off"), 1);
    for (int i = 1; i <= numChokes; ++i) chokeBox.addItem (juce::String (i), i + 1);
    chokeBox.setSelectedId (juce::jmax (1, sel), juce::dontSendNotification);
}

void Content::selectPad (int pad)
{
    processor.setSelectedPad (pad);
    if (pad != attachedPad) attachPad (pad);
    refreshPad (true);
    pads.repaint();
}

void Content::attachPad (int pad)
{
    attachedPad = pad;
    const char* knobIds[] { "shape", "tune", "tone", "comp", "volume", "pan" };
    for (int i = 0; i < knobs.size(); ++i)
    {
        knobs[i]->attach (processor.apvts, padId (pad, knobIds[i]));
        knobs[i]->setColour (colours::row (pad));
    }
    gateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, padId (pad, "gate"), gateButton);
    muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, padId (pad, "mute"), muteButton);
    soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, padId (pad, "solo"), soloButton);
    gateButton.getProperties().set ("lit", (juce::int64) colours::row (pad).getARGB());
}

void Content::fillOutputs()
{
    const int pad = processor.getSelectedPad();
    const auto r = processor.padInfo (pad).routing;
    const int outs = juce::jmax (1, processor.numOutputs());
    outBox.clear (juce::dontSendNotification);
    if (r.stereo)
        for (int c = 0; c + 1 < maxOutputs; c += 2)
        {
            if (c + 1 >= outs && c != r.out && c > 0) break;
            outBox.addItem (juce::String (c + 1) + "-" + juce::String (c + 2), c + 1);
        }
    else
        for (int c = 0; c < maxOutputs; ++c)
        {
            if (c >= outs && c != r.out && c > 0) break;
            outBox.addItem (juce::String (c + 1), c + 1);
        }
    outBox.setSelectedId (r.out + 1, juce::dontSendNotification);
    if (outBox.getSelectedId() == 0) outBox.setText (juce::String (r.out + 1), juce::dontSendNotification);
    stereoSwitch.repaint();
    shownOutputs = outs;
}

void Content::refreshPad (bool force)
{
    const int pad = processor.getSelectedPad();
    const auto info = processor.padInfo (pad);
    if (force || pad != selected)
    {
        selected = pad;
        hitBar.setPad (pad, info, lang);
        chokeBox.setSelectedId (info.routing.choke + 1, juce::dontSendNotification);
        fillOutputs();
        noteValue.repaint();
        for (auto* k : knobs) k->setEnabled (info.loaded());
    }
    screen.setPad (pad, info, processor.padParams (pad), lang);
    learnButton.setButtonText (processor.learningPad() == pad ? tr (lang, "learning") : u8 (lang == Lang::es ? "Aprender" : "Learn"));
    learnButton.setToggleState (processor.learningPad() == pad, juce::dontSendNotification);
}

void Content::timerCallback()
{
    processor.readTelemetry (telemetry);
    if (processor.pollLearn()) { refreshPad (true); pads.repaint(); }
    const int v = processor.getVersion();
    if (v != shownVersion)
    {
        shownVersion = v;
        updateTexts();
        refreshPad (true);
        pads.refresh();
    }
    else refreshPad (false);
    if (processor.numOutputs() != shownOutputs) { fillOutputs(); repaint(); }
    pads.update (telemetry, lang);
    screen.update (telemetry);
    output.repaint();
    velocity.repaint();
    if (messageTicks > 0 && --messageTicks == 0) { message.clear(); repaint(); }
}

//==============================================================================
void Content::loadFiles (int pad, const juce::StringArray& files)
{
    int target = pad;
    juce::String error;
    for (const auto& path : files)
    {
        const juce::File f (path);
        if (f.isDirectory() || target >= numPads) continue;
        if (f.hasFileExtension ("kupata")) { processor.loadKit (f); return; }
        if (processor.loadFile (target, f, error)) ++target;
        else { message = tr (lang, error.toRawUTF8()) + ": " + f.getFileName(); messageTicks = 120; }
    }
    if (target > pad) selectPad (pad);
    pads.refresh();
    repaint();
}

void Content::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    loadFiles (processor.getSelectedPad(), files);
}

void Content::chooseFile (int pad)
{
    chooser = std::make_unique<juce::FileChooser> (tr (lang, "openFile"), juce::File(), processor.formats.getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, pad] (const juce::FileChooser& fc) {
                              if (fc.getResult().existsAsFile()) loadFiles (pad, juce::StringArray (fc.getResult().getFullPathName()));
                          });
}

void Content::showPadMenu (int pad)
{
    const auto info = processor.padInfo (pad);
    juce::PopupMenu m, factory;
    for (int i = 0; i < numPads; ++i)
        factory.addItem (100 + i, u8 (lang == Lang::es ? factoryKit()[(size_t) i].es : factoryKit()[(size_t) i].en));
    m.addItem (1, tr (lang, "load"));
    if (info.missing) m.addItem (2, tr (lang, "locate"));
    m.addSubMenu (tr (lang, "factorySound"), factory);
    m.addSeparator();
    m.addItem (3, tr (lang, "learn"));
    m.addItem (4, tr (lang, "copyPad"), info.loaded());
    m.addItem (5, tr (lang, "pastePad"), copiedPad >= 0 && copiedPad != pad);
    m.addItem (6, tr (lang, "resetKnobs"));
    m.addSeparator();
    m.addItem (7, tr (lang, "clear"), info.loaded() || info.missing);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pads).withTargetScreenArea (
                         pads.localAreaToGlobal (pads.cell (pad).toNearestInt())),
                     [this, pad] (int r) {
        if (r == 1 || r == 2) chooseFile (pad);
        if (r == 3) processor.startLearn (pad);
        if (r == 4) copiedPad = pad;
        if (r == 5) processor.movePad (copiedPad, pad, true);
        if (r == 6) processor.resetKnobs (pad);
        if (r == 7) processor.clearPad (pad);
        if (r >= 100) processor.loadFactory (pad, r - 100);
        pads.refresh();
        refreshPad (true);
    });
}

void Content::showKitMenu()
{
    juce::PopupMenu m;
    m.addItem (1, tr (lang, "factoryKit"));
    m.addSeparator();
    m.addSectionHeader (tr (lang, "yourKits"));
    const auto folder = processor.kitsFolder();
    juce::Array<juce::File> kits;
    if (folder.isDirectory())
        for (const auto& e : juce::RangedDirectoryIterator (folder, false, "*.kupata")) kits.add (e.getFile());
    std::sort (kits.begin(), kits.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    if (kits.isEmpty()) m.addItem (-1, tr (lang, "noKits"), false);
    for (int i = 0; i < kits.size(); ++i) m.addItem (100 + i, kits[i].getFileNameWithoutExtension());
    m.addSeparator();
    m.addItem (2, tr (lang, "saveKit"));
    m.addItem (3, tr (lang, "loadKit"));
    m.addItem (4, tr (lang, "openKitsFolder"));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&kitButton), [this, kits] (int r) {
        if (r == 1) processor.loadFactoryKit();
        if (r == 2) saveKitDialog();
        if (r == 3)
        {
            chooser = std::make_unique<juce::FileChooser> (tr (lang, "loadKit"), processor.kitsFolder(), "*.kupata");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
                if (fc.getResult().existsAsFile() && ! processor.loadKit (fc.getResult())) { message = tr (lang, "kitError"); messageTicks = 120; repaint(); }
            });
        }
        if (r == 4) { processor.kitsFolder().createDirectory(); processor.kitsFolder().startAsProcess(); }
        if (r >= 100 && r - 100 < kits.size() && ! processor.loadKit (kits[r - 100])) { message = tr (lang, "kitError"); messageTicks = 120; repaint(); }
        pads.refresh();
        refreshPad (true);
    });
}

void Content::saveKitDialog()
{
    dialog = std::make_unique<juce::AlertWindow> (tr (lang, "saveKit"), tr (lang, "kitName"), juce::MessageBoxIconType::NoIcon, this);
    const auto current = processor.getKitName();
    dialog->addTextEditor ("name", current.isNotEmpty() ? current : tr (lang, "untitled"));
    dialog->addButton (tr (lang, "save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton (tr (lang, "cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->setLookAndFeel (&lnf);
    juce::Component::SafePointer<Content> safe (this);
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, safe] (int result) {
        if (result == 1 && dialog != nullptr)
        {
            auto name = dialog->getTextEditorContents ("name").trim();
            if (name.isEmpty()) name = tr (lang, "untitled");
            const auto file = processor.kitsFolder().getChildFile (juce::File::createLegalFileName (name) + ".kupata");
            if (! processor.saveKit (file, name)) { message = tr (lang, "kitError"); messageTicks = 120; }
            updateTexts();
            repaint();
        }
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->dialog.reset(); });
    }), false);
}

void Content::showNotesMenu()
{
    juce::PopupMenu m;
    m.addItem (1, tr (lang, "chromatic"));
    m.addItem (2, tr (lang, "whiteKeys"));
    m.addItem (3, tr (lang, "generalMidi"));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&notesButton), [this] (int r) {
        if (r >= 1) processor.setNotePreset (r - 1);
        pads.repaint();
        noteValue.repaint();
    });
}

//==============================================================================
void Content::resized()
{
    // header
    langEs.setBounds (baseWidth - 16 - 40, 16, 40, 28);
    langEn.setBounds (baseWidth - 16 - 40 - 6 - 40, 16, 40, 28);
    kitButton.setBounds (langEn.getX() - 16 - 250, 16, 250, 28);

    browser.setBounds (16, 72, 214, baseHeight - 72 - 16);

    // pads, and the kit's settings under them
    const int padX = 246, padW = 470;
    pads.setBounds (padX, 76, padW, padW);
    const int rowY = 76 + padW + 22;
    output.setBounds (padX, rowY, 150, 30);
    velocity.setBounds (padX + 160, rowY, 150, 30);
    notesButton.setBounds (padX + 320, rowY, padW - 320, 30);
    multiOutButton.setBounds (padX + 320, rowY + 40, padW - 320, 30);

    // the selected pad
    const int px = padX + padW + 18, pw = baseWidth - 16 - px;
    screen.setBounds (px, 76, pw, 214);
    hitBar.setBounds (px, 300, pw, 28);
    const int kx[] { px, px + (pw - Knob::width) / 2, px + pw - Knob::width };
    for (int i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (kx[i % 3], 342 + (i / 3) * (Knob::height + 6), Knob::width, Knob::height);

    const int ty = 342 + 2 * (Knob::height + 6) + 8;
    const int bw = 74;
    gateButton.setBounds (px, ty, bw, 30);
    muteButton.setBounds (px + bw + 8, ty, bw, 30);
    soloButton.setBounds (px + 2 * (bw + 8), ty, bw, 30);
    chokeBox.setBounds (px + pw - 84, ty, 84, 30);

    const int ry = ty + 42;
    outBox.setBounds (px + 52, ry, 92, 30);
    stereoSwitch.setBounds (px + 150, ry, 70, 30);
    learnButton.setBounds (px + pw - 84, ry, 84, 30);
    noteValue.setBounds (learnButton.getX() - 8 - 68, ry, 68, 30);
}

void Content::paint (juce::Graphics& g)
{
    // the case, with a faint grain
    g.fillAll (colours::body);
    juce::Random rng (11);
    for (int x = 0; x < baseWidth; x += 3)
    {
        g.setColour (juce::Colours::white.withAlpha (0.006f + 0.01f * rng.nextFloat()));
        g.fillRect ((float) x, 0.0f, 1.0f, (float) baseHeight);
    }
    g.setColour (colours::bodyEdge);
    g.fillRect (0.0f, 59.0f, (float) baseWidth, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRect (0.0f, 60.0f, (float) baseWidth, 1.0f);

    // name, in heavy lowercase, with its four colours under the first letters
    g.setColour (colours::text);
    g.setFont (heavy (34.0f).withExtraKerningFactor (-0.02f));
    g.drawText ("kupata", juce::Rectangle<float> (16.0f, 6.0f, 160.0f, 40.0f), juce::Justification::centredLeft, false);
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (colours::row (i * 4));
        g.fillRoundedRectangle (18.0f + i * 22.0f, 47.0f, 18.0f, 4.0f, 1.5f);
    }
    g.setColour (colours::sub);
    g.setFont (sans (14.0f));
    g.drawText (tr (lang, "subtitle"), juce::Rectangle<float> (140.0f, 14.0f, 260.0f, 28.0f), juce::Justification::centredLeft, false);

    // outputs of the track
    const int outs = processor.numOutputs();
    g.setColour (colours::faint);
    g.setFont (mono (12.0f));
    g.drawText (juce::String (outs) + " " + tr (lang, outs == 1 ? "outputOne" : "outputs"),
                juce::Rectangle<float> ((float) kitButton.getX() - 150.0f, 16.0f, 136.0f, 28.0f), juce::Justification::centredRight, false);

    // labels of the routing row
    auto label = [&] (const juce::String& t, juce::Rectangle<int> r) {
        g.setColour (colours::sub);
        g.setFont (semi (12.5f));
        g.drawText (t, r.toFloat(), juce::Justification::centredLeft, false);
    };
    label (tr (lang, "out"), { screen.getX(), outBox.getY(), 50, 30 });
    label (tr (lang, "choke"), { chokeBox.getX() - 52, chokeBox.getY(), 50, 30 });
    label (tr (lang, "note"), { noteValue.getX() - 42, noteValue.getY(), 40, 30 });

    // a line under the pads for messages and hints
    g.setColour (message.isNotEmpty() ? colours::accent : colours::faint);
    g.setFont (sans (13.0f));
    const auto hint = message.isNotEmpty() ? message : (processor.learningPad() >= 0 ? tr (lang, "learning") : tr (lang, "dropHere"));
    g.drawFittedText (hint, juce::Rectangle<int> (pads.getX(), multiOutButton.getY(), 310, 30), juce::Justification::centredLeft, 2);
}

void Content::paintOverChildren (juce::Graphics& g)
{
    if (! dropHover) return;
    g.setColour (colours::accent.withAlpha (0.08f));
    g.fillRect (getLocalBounds());
}

} // namespace kupata::ui

//==============================================================================
KupataEditor::KupataEditor (KupataProcessor& p)
    : AudioProcessorEditor (p), content (p)
{
    using C = kupata::ui::Content;
    addAndMakeVisible (content);
    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) C::baseWidth / C::baseHeight);
        c->setSizeLimits (C::baseWidth * 6 / 10, C::baseHeight * 6 / 10, C::baseWidth * 3 / 2, C::baseHeight * 3 / 2);
    }
    setSize (C::baseWidth * 9 / 10, C::baseHeight * 9 / 10);
}

void KupataEditor::resized()
{
    using C = kupata::ui::Content;
    const float scale = (float) getWidth() / (float) C::baseWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, C::baseWidth, C::baseHeight);
}
