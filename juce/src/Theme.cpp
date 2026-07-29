#include "Theme.h"

namespace hachi
{
const juce::Colour Palette::background  { 0xff353535 };
const juce::Colour Palette::panel       { 0xff2a2a2a };
const juce::Colour Palette::panelRaised { 0xff404040 };
const juce::Colour Palette::grid        { 0xff373737 };
const juce::Colour Palette::beatGrid    { 0xff4a4a4a };
const juce::Colour Palette::accent      { 0xff7f69ca };
const juce::Colour Palette::accentLight { 0xffcbcbfa };
const juce::Colour Palette::noteFill    { 0xfff4c000 };
const juce::Colour Palette::noteLight   { 0xffcbcbfa };
const juce::Colour Palette::noteEdge    { 0xff7f69ca };
const juce::Colour Palette::pitchLine   { 0xfff4f4f4 };
const juce::Colour Palette::playhead    { 0xfff05a5a };
const juce::Colour Palette::text        { 0xffd0d0d0 };
const juce::Colour Palette::textMuted   { 0xff909090 };

HachiLookAndFeel::HachiLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, Palette::background);
    setColour(juce::Label::textColourId, Palette::text);
    setColour(juce::TextButton::textColourOffId, Palette::text);
    setColour(juce::ComboBox::backgroundColourId, Palette::panelRaised);
    setColour(juce::ComboBox::textColourId, Palette::text);
    setColour(juce::ComboBox::outlineColourId, Palette::grid);
    setColour(juce::PopupMenu::backgroundColourId, Palette::panelRaised);
    setColour(juce::PopupMenu::textColourId, Palette::text);
    setColour(juce::ScrollBar::thumbColourId, Palette::accent.withAlpha(0.75f));
}

void HachiLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                            const juce::Colour&, bool highlighted, bool down)
{
    auto colour = button.getToggleState() ? Palette::accent : Palette::panelRaised;
    if (highlighted) colour = colour.brighter(0.12f);
    if (down) colour = colour.darker(0.15f);
    g.setColour(colour);
    g.fillRoundedRectangle(button.getLocalBounds().toFloat().reduced(1.0f), 4.0f);
}

void HachiLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                    int, int, int, int, juce::ComboBox&)
{
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    g.setColour(Palette::panelRaised);
    g.fillRoundedRectangle(bounds.reduced(1.0f), 4.0f);
    g.setColour(Palette::accentLight);
    const auto x = static_cast<float>(width - 14);
    const auto y = static_cast<float>(height) * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath(x - 4.0f, y - 2.0f);
    arrow.lineTo(x, y + 2.0f);
    arrow.lineTo(x + 4.0f, y - 2.0f);
    g.strokePath(arrow, juce::PathStrokeType(1.5f));
}
}
