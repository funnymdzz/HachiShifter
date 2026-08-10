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
    const auto svgIcon = [&bounds](const juce::String& data)
    {
        auto p = juce::Drawable::parseSVGPath(data);
        p.scaleToFit(bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), true);
        return p;
    };
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
        path = svgIcon("M3.29227 0.048984C3.47033 -0.032338 3.67946 -0.00228214 3.8274 0.125891L12.8587 7.95026C13.0134 8.08432 13.0708 8.29916 13.0035 8.49251C12.9362 8.68586 12.7578 8.81866 12.5533 8.82768L9.21887 8.97474L11.1504 13.2187C11.2648 13.47 11.1538 13.7664 10.9026 13.8808L8.75024 14.8613C8.499 14.9758 8.20255 14.8649 8.08802 14.6137L6.15339 10.3703L3.86279 12.7855C3.72196 12.934 3.50487 12.9817 3.31479 12.9059C3.1247 12.8301 3 12.6461 3 12.4414V0.503792C3 0.308048 3.11422 0.130306 3.29227 0.048984ZM4 1.59852V11.1877L5.93799 9.14425C6.05238 9.02363 6.21924 8.96776 6.38319 8.99516C6.54715 9.02256 6.68677 9.12965 6.75573 9.2809L8.79056 13.7441L10.0332 13.178L8.00195 8.71497C7.93313 8.56376 7.94391 8.38824 8.03072 8.24659C8.11753 8.10494 8.26903 8.01566 8.435 8.00834L11.2549 7.88397L4 1.59852Z");
    else if (id == "icon.draw")
        path = svgIcon("M11.8536 1.14645C11.6583 0.951184 11.3417 0.951184 11.1465 1.14645L3.71455 8.57836C3.62459 8.66832 3.55263 8.77461 3.50251 8.89155L2.04044 12.303C1.9599 12.491 2.00189 12.709 2.14646 12.8536C2.29103 12.9981 2.50905 13.0401 2.69697 12.9596L6.10847 11.4975C6.2254 11.4474 6.3317 11.3754 6.42166 11.2855L13.8536 3.85355C14.0488 3.65829 14.0488 3.34171 13.8536 3.14645L11.8536 1.14645ZM4.42166 9.28547L11.5 2.20711L12.7929 3.5L5.71455 10.5784L4.21924 11.2192L3.78081 10.7808L4.42166 9.28547Z");
    else if (id == "icon.line")
        path = svgIcon("M1.5 7.5C3 7.5 3 3.5 4.5 3.5C6 3.5 6 11.5 7.5 11.5C9 11.5 9 3.5 10.5 3.5C12 3.5 12 7.5 13.5 7.5");
    else if (id == "icon.wrench")
        path = svgIcon("M9.1 2.1a3.1 3.1 0 0 0-3.8 3.8L2.1 9.1a1.55 1.55 0 1 0 2.2 2.2l3.2-3.2a3.1 3.1 0 0 0 3.8-3.8L9.5 6.1 7.9 4.5 9.1 2.1Z");
    else if (id == "icon.connect")
    {
        path.addEllipse(bounds.getX(), bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
        path.addEllipse(bounds.getRight() - 6.0f, bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
        path.startNewSubPath(bounds.getX() + 5.0f, bounds.getCentreY());
        path.lineTo(bounds.getRight() - 5.0f, bounds.getCentreY());
    }
    else if (id == "icon.split")
    {
        path.addEllipse(bounds.getX(), bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
        path.addEllipse(bounds.getRight() - 6.0f, bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
        path.startNewSubPath(bounds.getX() + 5.0f, bounds.getCentreY());
        path.lineTo(bounds.getCentreX() - 2.0f, bounds.getCentreY());
        path.startNewSubPath(bounds.getCentreX() + 2.0f, bounds.getCentreY());
        path.lineTo(bounds.getRight() - 5.0f, bounds.getCentreY());
    }
    else
    {
        g.setFont(10.0f);
        g.drawText(id == "icon.audio" ? "A+" : "M+", button.getLocalBounds(), juce::Justification::centred);
        return;
    }
    if (id == "icon.line" || id == "icon.wrench" || id == "icon.connect" || id == "icon.split")
        g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    else
    {
        path.setUsingNonZeroWinding(false);
        g.fillPath(path);
    }
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
