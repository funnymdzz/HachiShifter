#include "PianoRollComponent.h"
#include "Theme.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace hachi
{
PianoRollComponent::PianoRollComponent(ProjectModel& modelToUse) : model(modelToUse)
{
    formats.registerBasicFormats();
    model.addChangeListener(this);
    rebuildLayout();
}

PianoRollComponent::~PianoRollComponent()
{
    model.removeChangeListener(this);
}

void PianoRollComponent::setPixelsPerSecond(float value)
{
    pixelsPerSecond = juce::jlimit(40.0f, 600.0f, value);
    rebuildLayout();
}

void PianoRollComponent::setSourceEditMode(bool enabled)
{
    sourceEditMode = enabled;
    repaint();
}

void PianoRollComponent::setPlayheadSeconds(double seconds)
{
    playheadSeconds = seconds;
    repaint();
}

float PianoRollComponent::timeToX(double seconds) const
{
    return 58.0f + static_cast<float>(seconds) * pixelsPerSecond;
}

float PianoRollComponent::midiToY(float midi) const
{
    return static_cast<float>(highestMidi) * rowHeight - midi * rowHeight;
}

float PianoRollComponent::yToMidi(float y) const
{
    return static_cast<float>(highestMidi) - y / rowHeight;
}

void PianoRollComponent::rebuildLayout()
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
            (highestMidi - lowestMidi + 1) * static_cast<int>(rowHeight));
    repaint();
}

void PianoRollComponent::drawClipWaveforms(juce::Graphics& g)
{
    for (const auto& track : snapshot.tracks)
    {
        if (!track.compose && !sourceEditMode) continue;
        for (const auto& clip : track.clips)
        {
            const auto found = thumbnails.find(clip.id.toStdString());
            if (found == thumbnails.end()) continue;
            float centreMidi = 60.0f;
            if (!clip.notes.empty())
            {
                double weighted = 0.0;
                double duration = 0.0;
                for (const auto& note : clip.notes)
                {
                    const auto weight = std::max(0.01, note.durationSeconds);
                    weighted += static_cast<double>(note.midiNote) * weight;
                    duration += weight;
                }
                centreMidi = static_cast<float>(weighted / std::max(0.01, duration));
            }
            const auto start = sourceEditMode ? 0.0 : clip.startSeconds;
            const auto waveformHeight = rowHeight * 10.0f;
            const juce::Rectangle<float> bounds(timeToX(start),
                midiToY(centreMidi) + rowHeight * 0.5f - waveformHeight * 0.5f,
                std::max(4.0f, static_cast<float>(clip.durationSeconds) * pixelsPerSecond), waveformHeight);
            g.setColour(Palette::textMuted.withAlpha(track.muted ? 0.12f : 0.34f));
            found->second->drawChannels(g, bounds.toNearestInt(), clip.sourceOffsetSeconds,
                                        clip.sourceOffsetSeconds + clip.durationSeconds, 1.0f);
        }
    }
}

void PianoRollComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    rebuildLayout();
}

void PianoRollComponent::paint(juce::Graphics& g)
{
    g.fillAll(Palette::background);
    noteHits.clear();

    for (int midi = lowestMidi; midi <= highestMidi; ++midi)
    {
        const auto y = midiToY(static_cast<float>(midi));
        const auto black = juce::MidiMessage::isMidiNoteBlack(midi);
        if (black)
        {
            g.setColour(juce::Colours::black.withAlpha(0.16f));
            g.fillRect(0.0f, y, static_cast<float>(getWidth()), rowHeight);
        }
        g.setColour(Palette::grid);
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, static_cast<float>(getWidth()));
    }

    auto keyboardX = 0;
    if (const auto* viewport = findParentComponentOfClass<juce::Viewport>())
        keyboardX = viewport->getViewPositionX();
    const auto drawKeyboard = [&]
    {
        g.setColour(Palette::panelRaised);
        g.fillRect(keyboardX, 0, 58, getHeight());
        for (int midi = lowestMidi; midi <= highestMidi; ++midi)
        {
            const auto y = midiToY(static_cast<float>(midi));
            if (juce::MidiMessage::isMidiNoteBlack(midi))
            {
                g.setColour(juce::Colours::black.withAlpha(0.55f));
                g.fillRect(static_cast<float>(keyboardX), y, 38.0f, rowHeight);
            }
            if (midi % 12 == 0)
            {
                g.setColour(Palette::textMuted);
                g.setFont(10.0f);
                g.drawText(juce::MidiMessage::getMidiNoteName(midi, true, true, 3),
                           keyboardX + 39, static_cast<int>(y), 18, static_cast<int>(rowHeight),
                           juce::Justification::centred);
            }
        }
    };

    const auto secondsPerBeat = 60.0 / std::max(1.0, snapshot.bpm);
    const auto firstBeat = std::floor((0.0 - snapshot.beatOriginSeconds) / secondsPerBeat) - 1.0;
    for (double beat = firstBeat;; beat += 1.0)
    {
        const auto seconds = snapshot.beatOriginSeconds + beat * secondsPerBeat;
        const auto x = timeToX(seconds);
        if (x > getWidth()) break;
        if (x < 58.0f) continue;
        const auto barBeat = static_cast<int>(std::llround(beat));
        const auto isBar = ((barBeat % std::max(1, snapshot.numerator)) + snapshot.numerator) % snapshot.numerator == 0;
        g.setColour(isBar ? Palette::beatGrid.brighter(0.35f) : Palette::beatGrid.withAlpha(0.55f));
        g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(getHeight()));
    }

    drawClipWaveforms(g);

    static const std::array<juce::Colour, 5> contourColours {
        Palette::accentLight, Palette::accent, Palette::noteFill,
        juce::Colour(0xff45b8aa), juce::Colour(0xffad7ad6)
    };
    std::size_t trackIndex = 0;
    for (const auto& track : snapshot.tracks)
    {
        if (!track.compose && !sourceEditMode) { ++trackIndex; continue; }
        const auto originalColour = contourColours[trackIndex % contourColours.size()];
        for (const auto& clip : track.clips)
        {
            for (const auto& note : clip.notes)
            {
                const auto absoluteStart = sourceEditMode ? note.startSeconds
                                                         : clip.startSeconds + note.startSeconds;
                const auto x = timeToX(absoluteStart);
                const auto y = midiToY(note.midiNote);
                const auto width = std::max(5.0f, static_cast<float>(note.durationSeconds) * pixelsPerSecond);
                const auto bounds = juce::Rectangle<float>(x, y + 2.0f, width, rowHeight - 4.0f);
                noteHits.push_back({ note.id, bounds, note.midiNote, note.startSeconds, note.durationSeconds });

                g.setColour(Palette::noteFill.darker(0.18f));
                g.fillRoundedRectangle(bounds, 4.0f);
                const auto consonantWidth = juce::jlimit(0.0f, width,
                    static_cast<float>(note.consonantSeconds) * pixelsPerSecond);
                g.setColour(Palette::noteLight.withAlpha(0.58f));
                g.fillRoundedRectangle(bounds.withWidth(consonantWidth), 4.0f);
                g.setColour(Palette::noteEdge);
                g.drawRoundedRectangle(bounds, 4.0f, 1.2f);

                g.setColour(Palette::textMuted.withAlpha(0.55f));
                const float dash[] { 4.0f, 3.0f };
                juce::Path boundary;
                boundary.startNewSubPath(bounds.getX(), bounds.getY() - rowHeight * 2.0f);
                boundary.lineTo(bounds.getX(), bounds.getBottom() + rowHeight * 2.0f);
                juce::Path dashedBoundary;
                juce::PathStrokeType(1.0f).createDashedStroke(dashedBoundary, boundary, dash, 2);
                g.fillPath(dashedBoundary);

                for (const auto marker : note.sibilantMarkers)
                {
                    g.setColour(Palette::noteLight);
                    const auto markerX = x + static_cast<float>(marker) * pixelsPerSecond;
                    g.drawVerticalLine(static_cast<int>(markerX), bounds.getY(), bounds.getBottom());
                }

                juce::Path originalContour;
                juce::Path contour;
                bool open = false;
                bool originalOpen = false;
                for (const auto& point : note.contour)
                {
                    if (!point.voiced)
                    {
                        open = false;
                        originalOpen = false;
                        continue;
                    }
                    const auto px = x + static_cast<float>(point.timeSeconds) * pixelsPerSecond;
                    const auto originalPitch = note.midiNote + point.relativeCents / 100.0f;
                    const auto pitch = note.midiNote
                        + point.relativeCents * note.modulation / 100.0f;
                    const auto py = midiToY(pitch) + rowHeight * 0.5f;
                    const auto originalY = midiToY(originalPitch) + rowHeight * 0.5f;
                    if (!originalOpen) originalContour.startNewSubPath(px, originalY);
                    else originalContour.lineTo(px, originalY);
                    originalOpen = true;
                    if (!open) contour.startNewSubPath(px, py);
                    else contour.lineTo(px, py);
                    open = true;
                }
                g.setColour(originalColour.withAlpha(0.86f));
                juce::Path dashed;
                juce::PathStrokeType(1.35f, juce::PathStrokeType::curved)
                    .createDashedStroke(dashed, originalContour, dash, 2);
                g.fillPath(dashed);
                g.setColour(Palette::pitchLine);
                g.strokePath(contour, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));

                if (note.connectedToPrevious)
                {
                    g.setColour(Palette::noteLight);
                    g.fillEllipse(bounds.getX() - 3.0f, bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
                }
            }
        }
        ++trackIndex;
    }

    if (draggedNote.isNotEmpty())
    {
        g.setColour(Palette::noteLight.withAlpha(0.8f));
        g.drawHorizontalLine(static_cast<int>(midiToY(previewMidi) + rowHeight * 0.5f),
                             58.0f, static_cast<float>(getWidth()));
    }

    g.setColour(juce::Colours::red.withAlpha(0.9f));
    g.drawVerticalLine(static_cast<int>(timeToX(playheadSeconds)), 0.0f, static_cast<float>(getHeight()));
    drawKeyboard();
}

void PianoRollComponent::mouseDown(const juce::MouseEvent& event)
{
    if (const auto* viewport = findParentComponentOfClass<juce::Viewport>())
        if (event.x < viewport->getViewPositionX() + 58)
            return;
    for (auto it = noteHits.rbegin(); it != noteHits.rend(); ++it)
        if (it->bounds.contains(event.position))
        {
            draggedNote = it->id;
            dragStartMidi = it->midi;
            previewMidi = dragStartMidi;
            dragStartSeconds = it->startSeconds;
            dragDurationSeconds = it->durationSeconds;
            previewStartSeconds = dragStartSeconds;
            previewDurationSeconds = dragDurationSeconds;
            if (!sourceEditMode && event.position.x <= it->bounds.getX() + 6.0f)
                dragMode = DragMode::resizeLeft;
            else if (!sourceEditMode && event.position.x >= it->bounds.getRight() - 6.0f)
                dragMode = DragMode::resizeRight;
            else
                dragMode = DragMode::pitch;
            return;
        }
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (draggedNote.isEmpty()) return;
    if (dragMode == DragMode::pitch)
        previewMidi = juce::jlimit(0.0f, 127.0f, std::round(yToMidi(event.position.y)));
    else if (dragMode == DragMode::resizeLeft)
    {
        const auto end = dragStartSeconds + dragDurationSeconds;
        previewStartSeconds = juce::jlimit(0.0, end - 0.01,
            dragStartSeconds + static_cast<double>(event.getDistanceFromDragStartX()) / pixelsPerSecond);
        previewDurationSeconds = end - previewStartSeconds;
    }
    else if (dragMode == DragMode::resizeRight)
        previewDurationSeconds = std::max(0.01, dragDurationSeconds
            + static_cast<double>(event.getDistanceFromDragStartX()) / pixelsPerSecond);
    repaint();
}

void PianoRollComponent::mouseUp(const juce::MouseEvent&)
{
    if (draggedNote.isEmpty()) return;
    if (dragMode == DragMode::pitch)
        model.transposeNote(draggedNote, previewMidi - dragStartMidi);
    else
        model.resizeNote(draggedNote, previewStartSeconds, previewDurationSeconds);
    draggedNote.clear();
    dragMode = DragMode::none;
}
}
