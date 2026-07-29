#include "Theme.h"

namespace hachi
{
const juce::Colour Palette::background  { 0xff202124 };
const juce::Colour Palette::panel       { 0xff292a2d };
const juce::Colour Palette::panelRaised { 0xff343538 };
const juce::Colour Palette::grid        { 0xff3b3c3f };
const juce::Colour Palette::beatGrid    { 0xff66542b };
const juce::Colour Palette::accent      { 0xffd98221 };
const juce::Colour Palette::accentLight { 0xffffc36b };
const juce::Colour Palette::noteEdge    { 0xffffa63d };
const juce::Colour Palette::pitchLine   { 0xfff4f4f4 };
const juce::Colour Palette::text        { 0xffeeeeee };
const juce::Colour Palette::textMuted   { 0xffa4a6aa };

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

