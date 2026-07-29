#include "TimelineComponent.h"
#include "Theme.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace hachi
{
namespace
{
const std::array<juce::Colour, 6> trackColours {
    juce::Colour(0xff7f69ca), juce::Colour(0xffcbcbfa), juce::Colour(0xfff4c000),
    juce::Colour(0xff9b8bdd), juce::Colour(0xffdedcff), juce::Colour(0xffffd94f)
};
}

TimelineComponent::TimelineComponent(ProjectModel& modelToUse) : model(modelToUse)
{
    formats.registerBasicFormats();
    model.addChangeListener(this);
    rebuild();
    startTimerHz(12);
}

TimelineComponent::~TimelineComponent()
{
    stopTimer();
    model.removeChangeListener(this);
}

float TimelineComponent::timeToX(double seconds) const
{
    return static_cast<float>(seconds) * pixelsPerSecond;
}

void TimelineComponent::setPixelsPerSecond(float value)
{
    pixelsPerSecond = juce::jlimit(40.0f, 600.0f, value);
    rebuild();
}

void TimelineComponent::setPlayheadSeconds(double seconds)
{
    playheadSeconds = seconds;
    repaint();
}

void TimelineComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    rebuild();
}

void TimelineComponent::timerCallback()
{
    repaint();
}

void TimelineComponent::rebuild()
{
    snapshot = model.snapshot();
    std::unordered_map<std::string, std::unique_ptr<juce::AudioThumbnail>> next;
    for (const auto& track : snapshot.tracks)
        for (const auto& clip : track.clips)
        {
            const auto key = clip.id.toStdString();
            if (const auto found = thumbnails.find(key); found != thumbnails.end())
            {
                next.emplace(key, std::move(found->second));
                continue;
            }
            auto thumbnail = std::make_unique<juce::AudioThumbnail>(256, formats, thumbnailCache);
            if (clip.sourceFile.existsAsFile())
                thumbnail->setSource(new juce::FileInputSource(clip.sourceFile));
            next.emplace(key, std::move(thumbnail));
        }
    thumbnails = std::move(next);
    setSize(static_cast<int>(timeToX(snapshot.durationSeconds()) + 400.0f),
            std::max(rowHeight, static_cast<int>(snapshot.tracks.size()) * rowHeight));
    repaint();
}

void TimelineComponent::paint(juce::Graphics& g)
{
    g.fillAll(Palette::background.darker(0.08f));
    const auto secondsPerBeat = 60.0 / std::max(1.0, snapshot.bpm);
    const auto firstBeat = static_cast<int>(std::floor(-snapshot.beatOriginSeconds / secondsPerBeat)) - 1;
    for (int beat = firstBeat;; ++beat)
    {
        const auto seconds = snapshot.beatOriginSeconds + static_cast<double>(beat) * secondsPerBeat;
        const auto x = timeToX(seconds);
        if (x > static_cast<float>(getWidth())) break;
        if (x < 0.0f) continue;
        const auto isBar = ((beat % std::max(1, snapshot.numerator)) + snapshot.numerator)
            % snapshot.numerator == 0;
        g.setColour(isBar ? Palette::grid.brighter(0.28f) : Palette::grid.withAlpha(0.52f));
        g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(getHeight()));
        if (isBar)
        {
            g.setColour(Palette::textMuted);
            g.setFont(10.0f);
            g.drawText(juce::String(beat / std::max(1, snapshot.numerator) + 1) + ".1",
                       static_cast<int>(x) + 4, 2, 42, 14, juce::Justification::left);
        }
    }

    for (std::size_t trackIndex = 0; trackIndex < snapshot.tracks.size(); ++trackIndex)
    {
        const auto& track = snapshot.tracks[trackIndex];
        const auto row = juce::Rectangle<int>(0, static_cast<int>(trackIndex) * rowHeight,
                                               getWidth(), rowHeight);
        g.setColour(Palette::grid);
        g.drawHorizontalLine(row.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));
        const auto colour = trackColours[trackIndex % trackColours.size()];
        for (const auto& clip : track.clips)
        {
            auto bounds = juce::Rectangle<float>(timeToX(clip.startSeconds),
                                                  static_cast<float>(row.getY() + 19),
                                                  std::max(4.0f, timeToX(clip.durationSeconds)),
                                                  static_cast<float>(rowHeight - 25));
            g.setColour((track.muted || clip.muted ? Palette::textMuted : colour).withAlpha(0.36f));
            g.fillRect(bounds);
            g.setColour(colour.withAlpha(0.82f));
            g.drawRect(bounds, 1.0f);

            if (const auto found = thumbnails.find(clip.id.toStdString()); found != thumbnails.end())
            {
                g.setColour(colour.brighter(0.65f).withAlpha(track.muted ? 0.25f : 0.78f));
                found->second->drawChannels(g, bounds.reduced(1.0f).toNearestInt(),
                                            clip.sourceOffsetSeconds,
                                            clip.sourceOffsetSeconds + clip.durationSeconds, 1.0f);
            }
            if (clip.fadeInSeconds > 0.0)
            {
                g.setColour(Palette::text.withAlpha(0.7f));
                g.drawLine(bounds.getX(), bounds.getBottom(),
                           bounds.getX() + timeToX(clip.fadeInSeconds), bounds.getY(), 1.0f);
            }
            if (clip.fadeOutSeconds > 0.0)
            {
                g.setColour(Palette::text.withAlpha(0.7f));
                g.drawLine(bounds.getRight() - timeToX(clip.fadeOutSeconds), bounds.getY(),
                           bounds.getRight(), bounds.getBottom(), 1.0f);
            }
            g.setColour(Palette::text);
            g.setFont(11.0f);
            g.drawText(clip.sourceFile.getFileNameWithoutExtension(),
                       bounds.toNearestInt().withHeight(17), juce::Justification::centredLeft, true);
        }
    }

    g.setColour(Palette::playhead);
    g.drawVerticalLine(static_cast<int>(timeToX(playheadSeconds)), 0.0f, static_cast<float>(getHeight()));
}

void TimelineComponent::mouseDown(const juce::MouseEvent& event)
{
    if (onSeek) onSeek(std::max(0.0, static_cast<double>(event.position.x) / pixelsPerSecond));
}
}
