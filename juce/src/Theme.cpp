#include "Theme.h"

namespace hachi
{
juce::Colour Palette::background  { 0xff353535 };
juce::Colour Palette::base        { 0xff2d2d2d };
juce::Colour Palette::panel       { 0xff2a2a2a };
juce::Colour Palette::panelRaised { 0xff404040 };
juce::Colour Palette::button      { 0xff3d3d3d };
juce::Colour Palette::buttonHover { 0xff484848 };
juce::Colour Palette::border      { 0xff505050 };
juce::Colour Palette::graphBackground { 0xff232323 };
juce::Colour Palette::clipBackground  { 0xff343434 };
juce::Colour Palette::grid        { 0xff373737 };
juce::Colour Palette::beatGrid    { 0xff4a4a4a };
juce::Colour Palette::accent      { 0xff7f69ca };
juce::Colour Palette::accentLight { 0xffcbcbfa };
juce::Colour Palette::noteFill    { 0xfff4c000 };
juce::Colour Palette::noteLight   { 0xffcbcbfa };
juce::Colour Palette::noteEdge    { 0xff7f69ca };
juce::Colour Palette::pitchLine   { 0xfff4f4f4 };
juce::Colour Palette::playhead    { 0xfff05a5a };
juce::Colour Palette::text        { 0xffd0d0d0 };
juce::Colour Palette::textMuted   { 0xff909090 };
juce::Colour Palette::scrollThumb { 0xff555555 };

void Palette::applyTheme(const juce::String& theme, juce::Colour accentColour,
                         juce::Colour accentLightColour, juce::Colour noteColour)
{
    const auto light = theme == "light";
    background = light ? juce::Colour(0xffeeeeee) : juce::Colour(0xff353535);
    base = light ? juce::Colour(0xfffafafa) : juce::Colour(0xff2d2d2d);
    panel = light ? juce::Colour(0xfff3f3f3) : juce::Colour(0xff2a2a2a);
    panelRaised = light ? juce::Colour(0xffe2e2e2) : juce::Colour(0xff404040);
    button = light ? juce::Colour(0xffdedede) : juce::Colour(0xff3d3d3d);
    buttonHover = light ? juce::Colour(0xffd2d2d2) : juce::Colour(0xff484848);
    border = light ? juce::Colour(0xffbcbcbc) : juce::Colour(0xff505050);
    graphBackground = light ? juce::Colour(0xfff8f8f8) : juce::Colour(0xff232323);
    clipBackground = light ? juce::Colour(0xffe9e9e9) : juce::Colour(0xff343434);
    grid = light ? juce::Colour(0xffdddddd) : juce::Colour(0xff373737);
    beatGrid = light ? juce::Colour(0xffc9c9c9) : juce::Colour(0xff4a4a4a);
    text = light ? juce::Colour(0xff282828) : juce::Colour(0xffd0d0d0);
    textMuted = light ? juce::Colour(0xff6f6f6f) : juce::Colour(0xff909090);
    scrollThumb = light ? juce::Colour(0xffa9a9a9) : juce::Colour(0xff555555);
    pitchLine = light ? juce::Colour(0xff242424) : juce::Colour(0xfff4f4f4);
    accent = accentColour;
    accentLight = accentLightColour;
    noteFill = noteColour;
    noteLight = accentLightColour;
    noteEdge = accentColour;
}

HachiLookAndFeel::HachiLookAndFeel()
{
    refreshColours();
}

void HachiLookAndFeel::refreshColours()
{
    setColour(juce::ResizableWindow::backgroundColourId, Palette::background);
    setColour(juce::Label::textColourId, Palette::text);
    setColour(juce::TextButton::textColourOffId, Palette::text);
    setColour(juce::ComboBox::backgroundColourId, Palette::base);
    setColour(juce::ComboBox::textColourId, Palette::text);
    setColour(juce::ComboBox::outlineColourId, Palette::border);
    setColour(juce::PopupMenu::backgroundColourId, Palette::panelRaised);
    setColour(juce::PopupMenu::textColourId, Palette::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Palette::accent);
    setColour(juce::PopupMenu::highlightedTextColourId, Palette::panel);
    setColour(juce::ScrollBar::backgroundColourId, Palette::base);
    setColour(juce::ScrollBar::trackColourId, Palette::base);
    setColour(juce::ScrollBar::thumbColourId, Palette::scrollThumb);
}

void HachiLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                            const juce::Colour&, bool highlighted, bool down)
{
    auto colour = button.getToggleState() ? Palette::accent : Palette::button;
    if (highlighted) colour = button.getToggleState() ? Palette::accent.brighter(0.12f)
                                                       : Palette::buttonHover;
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
    else if (id == "icon.pause")
    {
        const auto barWidth = juce::jmax(2.0f, bounds.getWidth() * 0.28f);
        path.addRoundedRectangle(bounds.getX() + 1.0f, bounds.getY(), barWidth, bounds.getHeight(), 1.0f);
        path.addRoundedRectangle(bounds.getRight() - barWidth - 1.0f, bounds.getY(),
                                 barWidth, bounds.getHeight(), 1.0f);
    }
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
    g.setColour(Palette::base);
    g.fillRoundedRectangle(bounds.reduced(1.0f), 4.0f);
    g.setColour(Palette::border);
    g.drawRoundedRectangle(bounds.reduced(1.0f), 4.0f, 1.0f);
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
