#include "TrackListComponent.h"
#include "Theme.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace hachi
{
namespace
{
juce::String algorithmLabel(const TrackData& track)
{
    const auto pitch = track.pitchAlgorithm == PitchAlgorithm::nsfHifigan ? juce::String("nsf-hifigan")
        : track.pitchAlgorithm == PitchAlgorithm::world ? juce::String("WORLD")
        : track.pitchAlgorithm == PitchAlgorithm::vocalShifter ? juce::String("vslib")
        : track.pitchAlgorithm == PitchAlgorithm::mld3 ? juce::String("mld3")
        : track.pitchAlgorithm == PitchAlgorithm::llsm2 ? juce::String("llsm2")
        : juce::String("mld5");
    const auto stretch = track.stretchAlgorithm == StretchAlgorithm::variableMelHop
        ? juce::String("variable-mel-hop")
        : track.stretchAlgorithm == StretchAlgorithm::loop ? juce::String("loop")
        : track.stretchAlgorithm == StretchAlgorithm::soundTouch ? juce::String("soundtouch")
        : track.stretchAlgorithm == StretchAlgorithm::nsfShiftThenSplice
            ? juce::String("nsf-shift-then-splice")
        : juce::String("melodyne-hybrid");
    return pitch + " / " + stretch;
}
}

TrackListComponent::TrackListComponent(ProjectModel& modelToUse, I18n& stringsToUse)
    : model(modelToUse), strings(stringsToUse), snapshot(model.snapshot())
{
    model.addChangeListener(this);
    setSize(226, rulerHeight + std::max(rowHeight, static_cast<int>(snapshot.tracks.size()) * rowHeight));
}

TrackListComponent::~TrackListComponent()
{
    model.removeChangeListener(this);
}

void TrackListComponent::setSelectedTrack(const juce::String& trackId)
{
    if (selectedTrack == trackId) return;
    selectedTrack = trackId;
    repaint();
}

void TrackListComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    snapshot = model.snapshot();
    const auto selectedStillExists = std::any_of(snapshot.tracks.begin(), snapshot.tracks.end(),
        [this](const auto& track) { return track.id == selectedTrack; });
    if (!selectedStillExists) selectedTrack.clear();
    setSize(226, rulerHeight + std::max(rowHeight, static_cast<int>(snapshot.tracks.size()) * rowHeight));
    repaint();
}

void TrackListComponent::paint(juce::Graphics& g)
{
    g.fillAll(Palette::panel);
    g.setColour(Palette::background);
    g.fillRect(0, 0, getWidth(), rulerHeight);
    g.setColour(Palette::border);
    g.drawHorizontalLine(rulerHeight - 1, 0.0f, static_cast<float>(getWidth()));
    if (snapshot.tracks.empty())
    {
        g.setColour(Palette::textMuted);
        g.drawFittedText(strings.text("status.noTracks"), getLocalBounds().reduced(14),
                         juce::Justification::centred, 3);
        return;
    }

    static const std::array<juce::Colour, 6> colours {
        Palette::accent, Palette::accentLight, Palette::noteFill,
        juce::Colour(0xff9b8bdd), juce::Colour(0xffdedcff), juce::Colour(0xffffd94f)
    };
    for (std::size_t index = 0; index < snapshot.tracks.size(); ++index)
    {
        const auto& track = snapshot.tracks[index];
        auto row = juce::Rectangle<int>(0, rulerHeight + static_cast<int>(index) * rowHeight,
                                        getWidth(), rowHeight);
        g.setColour(index % 2 == 0 ? Palette::panel : Palette::panelRaised.darker(0.1f));
        g.fillRect(row);
        if (track.id == selectedTrack)
        {
            g.setColour(Palette::accent.withAlpha(0.14f));
            g.fillRect(row);
            g.setColour(Palette::accent);
            g.fillRect(row.getX(), row.getY(), 3, row.getHeight());
        }
        g.setColour(Palette::grid);
        g.drawLine(0.0f, static_cast<float>(row.getBottom() - 1),
                   static_cast<float>(getWidth()), static_cast<float>(row.getBottom() - 1));

        const auto colour = colours[index % colours.size()];
        g.setColour(colour);
        g.fillEllipse(9.0f, static_cast<float>(row.getY() + 12), 9.0f, 9.0f);
        g.setColour(track.muted ? Palette::textMuted : Palette::text);
        g.setFont(13.0f);
        const auto displayedName = track.name
            + (track.compose ? "  [" + algorithmLabel(track) + "]" : juce::String());
        g.drawText(displayedName, row.getX() + 24, row.getY() + 5, row.getWidth() - 60, 23,
                   juce::Justification::centredLeft, true);

        const auto buttonY = static_cast<float>(row.getY() + 34);
        const auto drawToggle = [&](float x, const char* label, bool active, juce::Colour activeColour)
        {
            const juce::Rectangle<float> bounds(x, buttonY, 28.0f, 21.0f);
            g.setColour(active ? activeColour : Palette::panelRaised);
            g.fillRoundedRectangle(bounds, 3.0f);
            g.setColour(active ? Palette::panel : Palette::textMuted);
            g.setFont(11.0f);
            g.drawText(label, bounds.toNearestInt(), juce::Justification::centred);
        };
        drawToggle(10.0f, "C", track.compose, Palette::accentLight);
        drawToggle(43.0f, "M", track.muted, Palette::noteFill);
        drawToggle(76.0f, "S", track.solo, Palette::accent);

        const auto slider = juce::Rectangle<float>(112.0f, buttonY + 6.0f,
                                                    std::max(20.0f, static_cast<float>(getWidth()) - 152.0f), 8.0f);
        g.setColour(Palette::background);
        g.fillRoundedRectangle(slider, 4.0f);
        const auto displayedVolume = track.id == volumeDragTrack ? volumeDragPreview : track.volume;
        const auto normalized = juce::jlimit(0.0f, 1.0f, displayedVolume * 0.5f);
        g.setColour(colour);
        g.fillRoundedRectangle(slider.withWidth(slider.getWidth() * normalized), 4.0f);
        g.setColour(Palette::accentLight);
        g.fillEllipse(slider.getX() + slider.getWidth() * normalized - 4.0f,
                      slider.getCentreY() - 4.0f, 8.0f, 8.0f);
        const auto db = displayedVolume <= 0.0001f ? juce::String("-inf")
            : juce::String(20.0f * std::log10(displayedVolume), 1);
        g.setColour(Palette::textMuted);
        g.setFont(10.0f);
        g.drawText(strings.text("track.volume") + "  " + db + " dB", 112, row.getY() + 61, getWidth() - 150, 19,
                   juce::Justification::centredLeft);
        g.drawText(track.compose ? strings.text("track.compose") : strings.text("track.audio"),
                   10, row.getY() + 61, 94, 19, juce::Justification::centredLeft);

        const auto displayedPan = track.id == panDragTrack ? panDragPreview : track.pan;
        const juce::Rectangle<float> panRail(112.0f, static_cast<float>(row.getY() + 84),
                                             std::max(20.0f, static_cast<float>(getWidth()) - 152.0f), 5.0f);
        g.setColour(Palette::background);
        g.fillRoundedRectangle(panRail, 2.5f);
        g.setColour(Palette::grid.brighter(0.25f));
        g.drawVerticalLine(static_cast<int>(panRail.getCentreX()), panRail.getY() - 2.0f,
                           panRail.getBottom() + 2.0f);
        const auto panX = panRail.getX() + (displayedPan + 1.0f) * 0.5f * panRail.getWidth();
        g.setColour(colour.brighter(0.4f));
        g.fillEllipse(panX - 3.5f, panRail.getCentreY() - 3.5f, 7.0f, 7.0f);
        g.setColour(Palette::textMuted);
        g.setFont(9.0f);
        g.drawText("P " + juce::String(displayedPan, 2), 80, row.getY() + 77, 30, 16,
                   juce::Justification::centredRight);

        const auto peak = peakProvider ? std::max(0.0f, peakProvider(track.id)) : 0.0f;
        const auto peakDb = peak > 1.0e-6f ? 20.0f * std::log10(peak) : -60.0f;
        const auto meterAmount = juce::jlimit(0.0f, 1.0f, (peakDb + 48.0f) / 51.0f);
        const juce::Rectangle<float> meterRail(static_cast<float>(getWidth() - 28),
                                                static_cast<float>(row.getY()), 28.0f,
                                                static_cast<float>(rowHeight));
        g.setColour(Palette::panel);
        g.fillRect(meterRail);
        g.setColour(peak >= 1.0f ? juce::Colours::red : Palette::textMuted);
        g.setFont(8.0f);
        g.drawText(peak >= 1.0f ? juce::String("+") + juce::String(std::max(0.0f, peakDb), 1)
                               : juce::String(peakDb, 0),
                   meterRail.withHeight(15.0f).toNearestInt(), juce::Justification::centred);
        auto well = meterRail.withTrimmedTop(17.0f).reduced(6.0f, 0.0f);
        g.setColour(juce::Colour(0xff1d1d1d));
        g.fillRect(well);
        auto fill = well.withTrimmedTop(well.getHeight() * (1.0f - meterAmount));
        g.setColour(peak >= 1.0f ? juce::Colours::red
                    : peakDb >= -6.0f ? juce::Colour(0xffff9f2f)
                    : peakDb >= -18.0f ? Palette::noteFill : juce::Colour(0xff34b56f));
        g.fillRect(fill);
    }
}

void TrackListComponent::mouseDown(const juce::MouseEvent& event)
{
    if (event.y < rulerHeight) return;
    const auto index = (event.y - rulerHeight) / rowHeight;
    if (index < 0 || index >= static_cast<int>(snapshot.tracks.size())) return;
    const auto& track = snapshot.tracks[static_cast<std::size_t>(index)];
    selectedTrack = track.id;
    if (onTrackSelected) onTrackSelected(selectedTrack);
    repaint();
    const auto localY = event.y - rulerHeight - index * rowHeight;
    if (localY >= 34 && localY < 56 && event.x >= 10 && event.x < 38)
        model.setTrackCompose(track.id, !track.compose);
    else if (localY >= 34 && localY < 56 && event.x >= 43 && event.x < 71)
        model.setTrackMuted(track.id, !track.muted);
    else if (localY >= 34 && localY < 56 && event.x >= 76 && event.x < 104)
        model.setTrackSolo(track.id, !track.solo);
    else if (localY >= 34 && localY < 58 && event.x >= 108 && event.x < getWidth() - 32)
    {
        volumeDragTrack = track.id;
        volumeDragRow = index;
        volumeDragPreview = track.volume;
        mouseDrag(event);
    }
    else if (localY >= 77 && localY < 96 && event.x >= 108 && event.x < getWidth() - 32)
    {
        panDragTrack = track.id;
        panDragPreview = track.pan;
        mouseDrag(event);
    }
}

void TrackListComponent::mouseDrag(const juce::MouseEvent& event)
{
    const auto width = std::max(20, getWidth() - 152);
    const auto normalized = juce::jlimit(0.0f, 1.0f,
        static_cast<float>(event.x - 112) / static_cast<float>(width));
    if (volumeDragTrack.isNotEmpty()) volumeDragPreview = normalized * 2.0f;
    else if (panDragTrack.isNotEmpty()) panDragPreview = normalized * 2.0f - 1.0f;
    else return;
    repaint();
}

void TrackListComponent::mouseUp(const juce::MouseEvent&)
{
    if (volumeDragTrack.isNotEmpty())
        model.setTrackVolume(volumeDragTrack, volumeDragPreview);
    if (panDragTrack.isNotEmpty())
        model.setTrackPan(panDragTrack, panDragPreview);
    volumeDragTrack.clear();
    panDragTrack.clear();
    volumeDragRow = -1;
}
}
