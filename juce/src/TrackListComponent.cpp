#include "TrackListComponent.h"
#include "Theme.h"

namespace hachi
{
TrackListComponent::TrackListComponent(ProjectModel& modelToUse, I18n& stringsToUse)
    : model(modelToUse), strings(stringsToUse), snapshot(model.snapshot())
{
    model.addChangeListener(this);
}

TrackListComponent::~TrackListComponent()
{
    model.removeChangeListener(this);
}

void TrackListComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    snapshot = model.snapshot();
    repaint();
}

void TrackListComponent::paint(juce::Graphics& g)
{
    g.fillAll(Palette::panel);
    if (snapshot.tracks.empty())
    {
        g.setColour(Palette::textMuted);
        g.drawFittedText(strings.text("status.noTracks"), getLocalBounds().reduced(14),
                         juce::Justification::centred, 3);
        return;
    }

    for (std::size_t index = 0; index < snapshot.tracks.size(); ++index)
    {
        const auto& track = snapshot.tracks[index];
        auto row = juce::Rectangle<int>(0, static_cast<int>(index) * rowHeight, getWidth(), rowHeight);
        g.setColour(index % 2 == 0 ? Palette::panel : Palette::panelRaised.darker(0.1f));
        g.fillRect(row);
        g.setColour(Palette::grid);
        g.drawLine(0.0f, static_cast<float>(row.getBottom() - 1),
                   static_cast<float>(getWidth()), static_cast<float>(row.getBottom() - 1));

        g.setColour(track.muted ? Palette::textMuted : Palette::text);
        g.setFont(15.0f);
        g.drawText(track.name, row.reduced(10).removeFromTop(25), juce::Justification::centredLeft);

        auto controls = row.reduced(10).removeFromBottom(24);
        auto compose = controls.removeFromLeft(84).toFloat();
        auto mute = controls.removeFromLeft(52).toFloat();
        g.setColour(track.compose ? Palette::accent : Palette::grid);
        g.fillRoundedRectangle(compose, 3.0f);
        g.setColour(Palette::text);
        g.setFont(11.0f);
        g.drawText(track.compose ? strings.text("track.compose") : strings.text("track.audio"),
                   compose.toNearestInt(), juce::Justification::centred);
        g.setColour(track.muted ? Palette::accent : Palette::grid);
        g.fillRoundedRectangle(mute.reduced(3.0f, 0.0f), 3.0f);
        g.setColour(Palette::text);
        g.drawText(strings.text("track.mute"), mute.toNearestInt(), juce::Justification::centred);
    }
}

void TrackListComponent::mouseDown(const juce::MouseEvent& event)
{
    const auto index = event.y / rowHeight;
    if (index < 0 || index >= static_cast<int>(snapshot.tracks.size())) return;
    const auto& track = snapshot.tracks[static_cast<std::size_t>(index)];
    const auto localY = event.y - index * rowHeight;
    if (localY < rowHeight - 34) return;
    if (event.x >= 10 && event.x < 94)
        model.setTrackCompose(track.id, !track.compose);
    else if (event.x >= 94 && event.x < 146)
        model.setTrackMuted(track.id, !track.muted);
}
}

