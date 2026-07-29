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

void HachiLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto id = button.getComponentID();
    if (!id.startsWith("icon."))
    {
        LookAndFeel_V4::drawButtonText(g, button, false, false);
        return;
    }

    auto bounds = button.getLocalBounds().toFloat().reduced(6.0f);
    g.setColour(button.getToggleState() ? Palette::panel : Palette::text);
    juce::Path path;
    if (id == "icon.play")
        path.addTriangle(bounds.getX() + 2.0f, bounds.getY(), bounds.getRight(), bounds.getCentreY(),
                         bounds.getX() + 2.0f, bounds.getBottom());
    else if (id == "icon.stop")
        path.addRectangle(bounds.reduced(2.0f));
    else if (id == "icon.open")
    {
        path.addRoundedRectangle(bounds.withTrimmedTop(4.0f), 2.0f);
        path.addRectangle(bounds.getX() + 2.0f, bounds.getY() + 1.0f, bounds.getWidth() * 0.42f, 5.0f);
    }
    else if (id == "icon.save")
    {
        path.addRoundedRectangle(bounds, 1.5f);
        path.addRectangle(bounds.reduced(3.0f).withHeight(5.0f));
        path.addRectangle(bounds.reduced(4.0f).withTrimmedTop(9.0f));
    }
    else if (id == "icon.pointer")
    {
        path.startNewSubPath(bounds.getX() + 2.0f, bounds.getY());
        path.lineTo(bounds.getX() + 3.0f, bounds.getBottom() - 2.0f);
        path.lineTo(bounds.getCentreX(), bounds.getCentreY() + 2.0f);
        path.lineTo(bounds.getRight() - 1.0f, bounds.getCentreY());
        path.closeSubPath();
    }
    else if (id == "icon.draw")
    {
        path.addRectangle(bounds.getCentreX() - 1.5f, bounds.getY(), 3.0f, bounds.getHeight() - 3.0f);
        path.applyTransform(juce::AffineTransform::rotation(-0.68f, bounds.getCentreX(), bounds.getCentreY()));
    }
    else if (id == "icon.line")
    {
        path.startNewSubPath(bounds.getX(), bounds.getBottom());
        path.lineTo(bounds.getRight(), bounds.getY());
    }
    else if (id == "icon.wrench")
    {
        path.addEllipse(bounds.getX(), bounds.getY(), 7.0f, 7.0f);
        path.addRectangle(bounds.getX() + 5.0f, bounds.getY() + 5.0f, bounds.getWidth() - 7.0f, 3.0f);
    }
    else if (id == "icon.connect")
    {
        path.addEllipse(bounds.getX(), bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
        path.addEllipse(bounds.getRight() - 6.0f, bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
        path.startNewSubPath(bounds.getX() + 5.0f, bounds.getCentreY());
        path.lineTo(bounds.getRight() - 5.0f, bounds.getCentreY());
    }
    else
    {
        g.setFont(10.0f);
        g.drawText(id == "icon.audio" ? "A+" : "M+", button.getLocalBounds(), juce::Justification::centred);
        return;
    }
    if (id == "icon.line" || id == "icon.wrench" || id == "icon.connect")
        g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    else
        g.fillPath(path);
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
