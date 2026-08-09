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

int TimelineComponent::pixelForSeconds(double seconds) const
{
    return static_cast<int>(std::round(timeToX(seconds)));
}

double TimelineComponent::secondsForPixel(int pixel) const
{
    return std::max(0.0, static_cast<double>(pixel) / pixelsPerSecond);
}

double TimelineComponent::gridSeconds() const
{
    auto text = snapshot.gridDivision.trim().toLowerCase();
    auto dotted = text.endsWithChar('.');
    auto triplet = text.endsWithChar('t');
    if (dotted || triplet) text = text.dropLastCharacters(1);
    const auto slash = text.indexOfChar('/');
    const auto denominator = slash >= 0 ? text.substring(slash + 1).getIntValue() : 16;
    auto duration = 60.0 / std::max(1.0, snapshot.bpm)
        * 4.0 / static_cast<double>(std::max(1, denominator));
    if (dotted) duration *= 1.5;
    if (triplet) duration *= 2.0 / 3.0;
    return std::max(0.005, duration);
}

juce::String TimelineComponent::trackIdForPixel(int pixel) const
{
    if (pixel < rulerHeight) return {};
    const auto index = (pixel - rulerHeight) / rowHeight;
    return index >= 0 && index < static_cast<int>(snapshot.tracks.size())
        ? snapshot.tracks[static_cast<std::size_t>(index)].id : juce::String{};
}

void TimelineComponent::setPixelsPerSecond(float value)
{
    pixelsPerSecond = juce::jlimit(40.0f, 600.0f, value);
    rebuild();
}

void TimelineComponent::setRowHeight(float value)
{
    rowHeight = juce::jlimit(40, 220, static_cast<int>(std::round(value)));
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
    const auto selectedStillExists = std::any_of(snapshot.tracks.begin(), snapshot.tracks.end(), [this](const auto& track)
    {
        return std::any_of(track.clips.begin(), track.clips.end(), [this](const auto& clip)
        {
            return clip.id == selectedClip;
        });
    });
    if (!selectedStillExists) selectedClip.clear();
    std::unordered_map<std::string, std::unique_ptr<juce::AudioThumbnail>> next;
    for (const auto& track : snapshot.tracks)
        for (const auto& clip : track.clips)
        {
            const auto key = clip.sourceFile.getFullPathName().toStdString();
            if (next.contains(key)) continue;
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
            rulerHeight + std::max(rowHeight, static_cast<int>(snapshot.tracks.size()) * rowHeight));
    repaint();
}

void TimelineComponent::paint(juce::Graphics& g)
{
    g.fillAll(Palette::base);
    clipHits.clear();
    g.setColour(Palette::background);
    g.fillRect(0, 0, getWidth(), rulerHeight);
    g.setColour(Palette::border);
    g.drawHorizontalLine(rulerHeight - 1, 0.0f, static_cast<float>(getWidth()));
    const auto secondsPerBeat = 60.0 / std::max(1.0, snapshot.bpm);
    const auto gridStep = gridSeconds();
    const auto firstTick = static_cast<int>(std::floor(-snapshot.beatOriginSeconds / gridStep)) - 1;
    for (int tick = firstTick;; ++tick)
    {
        const auto seconds = snapshot.beatOriginSeconds + static_cast<double>(tick) * gridStep;
        const auto x = timeToX(seconds);
        if (x > static_cast<float>(getWidth())) break;
        if (x < 0.0f) continue;
        const auto beat = seconds / secondsPerBeat;
        const auto isBeat = std::abs(beat - std::llround(beat)) < 1.0e-6;
        const auto barBeat = static_cast<int>(std::llround(beat));
        const auto isBar = isBeat && ((barBeat % std::max(1, snapshot.numerator)) + snapshot.numerator)
            % snapshot.numerator == 0;
        g.setColour(isBar ? Palette::grid.brighter(0.28f)
                   : isBeat ? Palette::grid.withAlpha(0.62f) : Palette::grid.withAlpha(0.30f));
        g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(getHeight()));
        if (isBar)
        {
            g.setColour(Palette::textMuted);
            g.setFont(10.0f);
            g.drawText(juce::String(barBeat / std::max(1, snapshot.numerator) + 1) + ".1",
                       static_cast<int>(x) + 4, 3, 42, 16, juce::Justification::left);
        }
    }

    for (std::size_t trackIndex = 0; trackIndex < snapshot.tracks.size(); ++trackIndex)
    {
        const auto& track = snapshot.tracks[trackIndex];
        const auto row = juce::Rectangle<int>(0, rulerHeight + static_cast<int>(trackIndex) * rowHeight,
                                               getWidth(), rowHeight);
        g.setColour(Palette::grid);
        g.drawHorizontalLine(row.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));
        const auto colour = trackColours[trackIndex % trackColours.size()];
        for (const auto& clip : track.clips)
        {
            const auto displayStart = clip.id == draggedClip ? draggedClipPreviewStart
                                                              : clip.startSeconds;
            const auto displayDuration = clip.id == draggedClip ? draggedClipPreviewDuration
                                                                 : clip.durationSeconds;
            const auto displayRatio = clip.durationSeconds > 1.0e-9
                ? displayDuration / clip.durationSeconds : 1.0;
            auto displayFadeIn = clip.fadeInSeconds * displayRatio;
            auto displayFadeOut = clip.fadeOutSeconds * displayRatio;
            if (clip.id == draggedClip
                && (dragMode == DragMode::fadeIn || dragMode == DragMode::fadeOut))
            {
                displayFadeIn = draggedClipPreviewFadeIn;
                displayFadeOut = draggedClipPreviewFadeOut;
            }
            auto bounds = juce::Rectangle<float>(timeToX(displayStart),
                                                  static_cast<float>(row.getY() + 19),
                                                  std::max(4.0f, static_cast<float>(displayDuration)
                                                                      * pixelsPerSecond),
                                                  static_cast<float>(rowHeight - 25));
            clipHits.push_back({ clip.id, bounds, clip.startSeconds, clip.durationSeconds,
                                 clip.fadeInSeconds, clip.fadeOutSeconds, clip.muted });
            g.setColour(track.muted || clip.muted ? Palette::clipBackground.withAlpha(0.55f)
                                                  : Palette::clipBackground);
            g.fillRect(bounds);
            g.setColour(colour.withAlpha(0.82f));
            g.drawRect(bounds, 1.0f);
            if (clip.id == selectedClip)
            {
                g.setColour(Palette::text.withAlpha(0.92f));
                g.drawRect(bounds.reduced(1.0f), 2.0f);
            }

            if (const auto found = thumbnails.find(clip.sourceFile.getFullPathName().toStdString()); found != thumbnails.end())
            {
                g.setColour(colour.brighter(0.65f).withAlpha(
                    track.muted || clip.muted ? 0.25f : 0.78f));
                // Keep the editor visually consistent for mono and stereo
                // sources.  Playback still uses every source channel; only the
                // compact waveform lane displays channel 1.
                found->second->drawChannel(
                    g, bounds.withTrimmedTop(17.0f).reduced(1.0f).toNearestInt(),
                    clip.sourceOffsetSeconds,
                    clip.sourceOffsetSeconds
                        + (clip.sourceDurationSeconds > 1.0e-9
                            ? clip.sourceDurationSeconds : clip.durationSeconds),
                    0, 1.0f);
            }
            const auto waveformTop = bounds.getY() + 19.0f;
            const auto fadeInX = bounds.getX() + timeToX(displayFadeIn);
            const auto fadeOutX = bounds.getRight() - timeToX(displayFadeOut);
            if (displayFadeIn > 0.0)
            {
                g.setColour(Palette::text.withAlpha(0.7f));
                g.drawLine(bounds.getX(), bounds.getBottom(),
                           fadeInX, waveformTop, 1.0f);
            }
            if (displayFadeOut > 0.0)
            {
                g.setColour(Palette::text.withAlpha(0.7f));
                g.drawLine(fadeOutX, waveformTop,
                           bounds.getRight(), bounds.getBottom(), 1.0f);
            }
            g.setColour(colour.brighter(0.72f).withAlpha(
                clip.id == selectedClip ? 0.95f : 0.52f));
            g.fillEllipse(fadeInX - 3.5f, waveformTop - 3.5f, 7.0f, 7.0f);
            g.fillEllipse(fadeOutX - 3.5f, waveformTop - 3.5f, 7.0f, 7.0f);
            g.setColour(Palette::text);
            g.setColour(Palette::panel.withAlpha(0.82f));
            g.fillRect(bounds.toNearestInt().withHeight(17));
            g.setColour(clip.muted ? Palette::noteFill : colour.brighter(0.5f));
            g.fillRoundedRectangle(bounds.getX() + 2.0f, bounds.getY() + 2.0f, 14.0f, 13.0f, 2.0f);
            g.setColour(Palette::panel);
            g.setFont(9.0f);
            g.drawText("M", static_cast<int>(bounds.getX() + 2.0f), static_cast<int>(bounds.getY() + 1.0f),
                       14, 14, juce::Justification::centred);
            g.setColour(Palette::text);
            g.setFont(10.0f);
            const auto gainDb = clip.gain <= 0.0001f ? juce::String("-inf")
                : juce::String(20.0f * std::log10(clip.gain), 1);
            g.drawText(clip.sourceFile.getFileNameWithoutExtension() + "  " + gainDb + " dB",
                       bounds.toNearestInt().withTrimmedLeft(19).withHeight(17),
                       juce::Justification::centredLeft, true);
            g.setColour(colour.brighter(0.65f));
            juce::Path leftHandle;
            leftHandle.addTriangle(bounds.getX(), bounds.getY(), bounds.getX() + 7.0f, bounds.getY(),
                                   bounds.getX(), bounds.getY() + 7.0f);
            g.fillPath(leftHandle);
            juce::Path rightHandle;
            rightHandle.addTriangle(bounds.getRight(), bounds.getY(), bounds.getRight() - 7.0f, bounds.getY(),
                                    bounds.getRight(), bounds.getY() + 7.0f);
            g.fillPath(rightHandle);
        }
    }

    g.setColour(Palette::playhead);
    g.drawVerticalLine(static_cast<int>(timeToX(playheadSeconds)), 0.0f, static_cast<float>(getHeight()));
}

void TimelineComponent::mouseMove(const juce::MouseEvent& event)
{
    for (auto it = clipHits.rbegin(); it != clipHits.rend(); ++it)
        if (it->bounds.contains(event.position))
        {
            const auto muteBounds = juce::Rectangle<float>(it->bounds.getX() + 2.0f,
                it->bounds.getY() + 2.0f, 14.0f, 13.0f);
            if (muteBounds.contains(event.position))
            {
                setMouseCursor(juce::MouseCursor::PointingHandCursor);
                return;
            }
            const auto waveformTop = it->bounds.getY() + 19.0f;
            const auto fadeIn = juce::Point<float>(it->bounds.getX()
                + timeToX(it->fadeInSeconds), waveformTop);
            const auto fadeOut = juce::Point<float>(it->bounds.getRight()
                - timeToX(it->fadeOutSeconds), waveformTop);
            if (event.position.getDistanceFrom(fadeIn) <= 8.0f
                || event.position.getDistanceFrom(fadeOut) <= 8.0f)
            {
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                return;
            }
            const auto onHandle = event.position.x <= it->bounds.getX() + 8.0f
                || event.position.x >= it->bounds.getRight() - 8.0f;
            setMouseCursor(onHandle ? juce::MouseCursor::LeftRightResizeCursor
                                    : juce::MouseCursor::NormalCursor);
            return;
        }
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void TimelineComponent::mouseExit(const juce::MouseEvent&)
{
    if (draggedClip.isEmpty()) setMouseCursor(juce::MouseCursor::NormalCursor);
}

void TimelineComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    for (auto it = clipHits.rbegin(); it != clipHits.rend(); ++it)
        if (it->bounds.contains(event.position))
        {
            const auto muteBounds = juce::Rectangle<float>(it->bounds.getX() + 2.0f,
                it->bounds.getY() + 2.0f, 14.0f, 13.0f);
            if (muteBounds.contains(event.position)) return;
            draggedClip.clear();
            dragMode = DragMode::none;
            selectedClip = it->id;
            if (onClipSelected) onClipSelected(selectedClip);
            if (onClipGainRequested) onClipGainRequested(selectedClip);
            repaint();
            return;
        }
}

void TimelineComponent::mouseDown(const juce::MouseEvent& event)
{
    for (auto it = clipHits.rbegin(); it != clipHits.rend(); ++it)
        if (it->bounds.contains(event.position))
        {
            selectedClip = it->id;
            if (onClipSelected) onClipSelected(selectedClip);
            const auto muteBounds = juce::Rectangle<float>(it->bounds.getX() + 2.0f,
                it->bounds.getY() + 2.0f, 14.0f, 13.0f);
            if (muteBounds.contains(event.position))
            {
                model.setClipMuted(it->id, !it->muted);
                return;
            }
            draggedClip = it->id;
            draggedClipStart = it->startSeconds;
            draggedClipDuration = it->durationSeconds;
            draggedClipPreviewStart = draggedClipStart;
            draggedClipPreviewDuration = draggedClipDuration;
            draggedClipFadeIn = it->fadeInSeconds;
            draggedClipFadeOut = it->fadeOutSeconds;
            draggedClipPreviewFadeIn = draggedClipFadeIn;
            draggedClipPreviewFadeOut = draggedClipFadeOut;
            dragAnchorX = event.position.x;
            const auto waveformTop = it->bounds.getY() + 19.0f;
            const auto fadeIn = juce::Point<float>(it->bounds.getX()
                + timeToX(it->fadeInSeconds), waveformTop);
            const auto fadeOut = juce::Point<float>(it->bounds.getRight()
                - timeToX(it->fadeOutSeconds), waveformTop);
            if (event.position.getDistanceFrom(fadeIn) <= 8.0f)
            {
                dragMode = DragMode::fadeIn;
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            }
            else if (event.position.getDistanceFrom(fadeOut) <= 8.0f)
            {
                dragMode = DragMode::fadeOut;
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            }
            else if (event.position.x <= it->bounds.getX() + 8.0f)
            {
                dragMode = DragMode::resizeLeft;
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            }
            else if (event.position.x >= it->bounds.getRight() - 8.0f)
            {
                dragMode = DragMode::resizeRight;
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            }
            else
            {
                dragMode = DragMode::move;
                setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            }
            repaint();
            return;
        }
    selectedClip.clear();
    if (onClipSelected) onClipSelected({});
    repaint();
    if (onSeek) onSeek(std::max(0.0, static_cast<double>(event.position.x) / pixelsPerSecond));
}

void TimelineComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (draggedClip.isEmpty()) return;
    const auto secondsPerBeat = 60.0 / std::max(1.0, snapshot.bpm);
    const auto division = snapshot.gridDivision.contains("32") ? 8.0
        : snapshot.gridDivision.contains("16") ? 4.0
        : snapshot.gridDivision.contains("8") ? 2.0
        : snapshot.gridDivision.contains("4") ? 1.0 : 0.5;
    const auto quantum = secondsPerBeat / division;
    const auto snap = [&](double seconds)
    {
        return std::max(0.0, snapshot.beatOriginSeconds
            + std::round((seconds - snapshot.beatOriginSeconds) / quantum) * quantum);
    };
    const auto delta = static_cast<double>(event.position.x - dragAnchorX) / pixelsPerSecond;
    if (dragMode == DragMode::fadeIn)
        draggedClipPreviewFadeIn = juce::jlimit(0.0, draggedClipDuration,
            static_cast<double>(event.position.x) / pixelsPerSecond - draggedClipStart);
    else if (dragMode == DragMode::fadeOut)
        draggedClipPreviewFadeOut = juce::jlimit(0.0, draggedClipDuration,
            draggedClipStart + draggedClipDuration
                - static_cast<double>(event.position.x) / pixelsPerSecond);
    else if (dragMode == DragMode::resizeLeft)
    {
        const auto end = draggedClipStart + draggedClipDuration;
        draggedClipPreviewStart = juce::jlimit(0.0, end - 0.01,
                                                snap(draggedClipStart + delta));
        draggedClipPreviewDuration = end - draggedClipPreviewStart;
    }
    else if (dragMode == DragMode::resizeRight)
    {
        const auto end = std::max(draggedClipStart + 0.01,
                                  snap(draggedClipStart + draggedClipDuration + delta));
        draggedClipPreviewDuration = end - draggedClipStart;
    }
    else
        draggedClipPreviewStart = snap(draggedClipStart + delta);
    repaint();
}

void TimelineComponent::mouseUp(const juce::MouseEvent&)
{
    if (draggedClip.isNotEmpty())
    {
        if (dragMode == DragMode::fadeIn || dragMode == DragMode::fadeOut)
            model.setClipFades(draggedClip, draggedClipPreviewFadeIn,
                               draggedClipPreviewFadeOut);
        else if (dragMode == DragMode::resizeLeft || dragMode == DragMode::resizeRight)
            model.resizeClip(draggedClip, draggedClipPreviewStart,
                             draggedClipPreviewDuration);
        else
            model.moveClip(draggedClip, draggedClipPreviewStart);
    }
    draggedClip.clear();
    dragMode = DragMode::none;
    setMouseCursor(juce::MouseCursor::NormalCursor);
}
}
