#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace hachi
{
struct Palette
{
    static juce::Colour background;
    static juce::Colour base;
    static juce::Colour panel;
    static juce::Colour panelRaised;
    static juce::Colour button;
    static juce::Colour buttonHover;
    static juce::Colour border;
    static juce::Colour graphBackground;
    static juce::Colour clipBackground;
    static juce::Colour grid;
    static juce::Colour beatGrid;
    static juce::Colour accent;
    static juce::Colour accentLight;
    static juce::Colour noteFill;
    static juce::Colour noteLight;
    static juce::Colour noteEdge;
    static juce::Colour pitchLine;
    static juce::Colour playhead;
    static juce::Colour text;
    static juce::Colour textMuted;
    static juce::Colour scrollThumb;
    static void applyTheme(const juce::String& theme, juce::Colour accentColour,
                           juce::Colour accentLightColour, juce::Colour noteColour);
};

class HachiLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    HachiLookAndFeel();
    void refreshColours();
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool down,
                      int, int, int, int, juce::ComboBox&) override;
};
}
