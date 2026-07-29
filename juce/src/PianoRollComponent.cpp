#include "PianoRollComponent.h"
#include "Theme.h"
#include <algorithm>
#include <cmath>

namespace hachi
{
PianoRollComponent::PianoRollComponent(ProjectModel& modelToUse) : model(modelToUse)
{
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
    setSize(static_cast<int>(timeToX(snapshot.durationSeconds()) + 400.0f),
            (highestMidi - lowestMidi + 1) * static_cast<int>(rowHeight));
    repaint();
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

    g.setColour(Palette::panelRaised);
    g.fillRect(0, 0, 58, getHeight());
    for (int midi = lowestMidi; midi <= highestMidi; ++midi)
    {
        const auto y = midiToY(static_cast<float>(midi));
        if (juce::MidiMessage::isMidiNoteBlack(midi))
        {
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.fillRect(0.0f, y, 38.0f, rowHeight);
        }
        if (midi % 12 == 0)
        {
            g.setColour(Palette::textMuted);
            g.setFont(10.0f);
            g.drawText(juce::MidiMessage::getMidiNoteName(midi, true, true, 3),
                       39, static_cast<int>(y), 18, static_cast<int>(rowHeight), juce::Justification::centred);
        }
    }

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

    for (const auto& track : snapshot.tracks)
    {
        if (!track.compose && !sourceEditMode) continue;
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
                noteHits.push_back({ note.id, bounds, note.midiNote });

                g.setColour(Palette::accent.darker(0.18f));
                g.fillRoundedRectangle(bounds, 4.0f);
                const auto consonantWidth = juce::jlimit(0.0f, width,
                    static_cast<float>(note.consonantSeconds) * pixelsPerSecond);
                g.setColour(Palette::accentLight.withAlpha(0.58f));
                g.fillRoundedRectangle(bounds.withWidth(consonantWidth), 4.0f);
                g.setColour(Palette::noteEdge);
                g.drawRoundedRectangle(bounds, 4.0f, 1.2f);

                for (const auto marker : note.sibilantMarkers)
                {
                    g.setColour(Palette::accentLight);
                    const auto markerX = x + static_cast<float>(marker) * pixelsPerSecond;
                    g.drawVerticalLine(static_cast<int>(markerX), bounds.getY(), bounds.getBottom());
                }

                juce::Path contour;
                bool open = false;
                for (const auto& point : note.contour)
                {
                    if (!point.voiced)
                    {
                        open = false;
                        continue;
                    }
                    const auto px = x + static_cast<float>(point.timeSeconds) * pixelsPerSecond;
                    const auto pitch = note.midiNote
                        + point.relativeCents * note.modulation / 100.0f;
                    const auto py = midiToY(pitch) + rowHeight * 0.5f;
                    if (!open) contour.startNewSubPath(px, py);
                    else contour.lineTo(px, py);
                    open = true;
                }
                g.setColour(Palette::pitchLine);
                g.strokePath(contour, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));

                if (note.connectedToPrevious)
                {
                    g.setColour(Palette::accentLight);
                    g.fillEllipse(bounds.getX() - 3.0f, bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
                }
            }
        }
    }

    if (draggedNote.isNotEmpty())
    {
        g.setColour(Palette::accentLight.withAlpha(0.8f));
        g.drawHorizontalLine(static_cast<int>(midiToY(previewMidi) + rowHeight * 0.5f),
                             58.0f, static_cast<float>(getWidth()));
    }

    g.setColour(juce::Colours::red.withAlpha(0.9f));
    g.drawVerticalLine(static_cast<int>(timeToX(playheadSeconds)), 0.0f, static_cast<float>(getHeight()));
}

void PianoRollComponent::mouseDown(const juce::MouseEvent& event)
{
    for (auto it = noteHits.rbegin(); it != noteHits.rend(); ++it)
        if (it->bounds.contains(event.position))
        {
            draggedNote = it->id;
            dragStartMidi = it->midi;
            previewMidi = dragStartMidi;
            return;
        }
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (draggedNote.isEmpty()) return;
    previewMidi = juce::jlimit(0.0f, 127.0f, std::round(yToMidi(event.position.y)));
    repaint();
}

void PianoRollComponent::mouseUp(const juce::MouseEvent&)
{
    if (draggedNote.isEmpty()) return;
    model.transposeNote(draggedNote, previewMidi - dragStartMidi);
    draggedNote.clear();
}
}
