#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace hachi
{
struct Palette
{
    static const juce::Colour background;
    static const juce::Colour panel;
    static const juce::Colour panelRaised;
    static const juce::Colour grid;
    static const juce::Colour beatGrid;
    static const juce::Colour accent;
    static const juce::Colour accentLight;
    static const juce::Colour noteEdge;
    static const juce::Colour pitchLine;
    static const juce::Colour text;
    static const juce::Colour textMuted;
};

class HachiLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    HachiLookAndFeel();
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool down,
                      int, int, int, int, juce::ComboBox&) override;
};
}

