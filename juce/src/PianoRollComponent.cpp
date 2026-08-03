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
    setWantsKeyboardFocus(true);
    rebuildLayout();
}

PianoRollComponent::~PianoRollComponent()
{
    for (auto& [_, thumbnail] : thumbnails)
        thumbnail->removeChangeListener(this);
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
    if (sourceEditMode && focusedClip.isEmpty())
        for (const auto& track : snapshot.tracks)
            if (!track.clips.empty())
            {
                focusedClip = track.clips.front().id;
                break;
            }
    updateCanvasSize();
    repaint();
}

void PianoRollComponent::setFocusedClip(const juce::String& clipId)
{
    focusedClip = clipId;
    updateCanvasSize();
    repaint();
}

void PianoRollComponent::setFocusedTrack(const juce::String& trackId)
{
    focusedTrack = trackId;
    repaint();
}

void PianoRollComponent::setShowNoteLabels(bool enabled)
{
    if (showNoteLabels == enabled) return;
    showNoteLabels = enabled;
    repaint();
}

void PianoRollComponent::setSampleRegions(const std::vector<SampleRegionSetting>& regions,
                                          int activeRegion)
{
    sampleRegions = regions;
    activeSampleRegion = juce::jlimit(-1, static_cast<int>(sampleRegions.size()) - 1,
                                      activeRegion);
    repaint();
}

void PianoRollComponent::setPlayheadSeconds(double seconds)
{
    playheadSeconds = seconds;
    repaint();
}

void PianoRollComponent::setTool(Tool nextTool)
{
    tool = nextTool;
    setMouseCursor(tool == Tool::draw || tool == Tool::line
                       ? juce::MouseCursor::CrosshairCursor
                       : juce::MouseCursor::NormalCursor);
}

void PianoRollComponent::selectAllNotes()
{
    selectedNotes.clear();
    for (const auto& track : snapshot.tracks)
    {
        if (!track.compose && !sourceEditMode) continue;
        if (!sourceEditMode && focusedTrack.isNotEmpty() && track.id != focusedTrack) continue;
        for (const auto& clip : track.clips)
        {
            // Wrench mode edits one source instance.  Drawing every project
            // use of the same WAV stacked duplicate vertical sliders on top
            // of one HJM region.
            if (sourceEditMode && clip.id != focusedClip) continue;
            for (const auto& note : clip.notes) selectedNotes.insert(note.id.toStdString());
        }
    }
    selectedNote = selectedNotes.empty() ? juce::String()
        : juce::String::fromUTF8(selectedNotes.begin()->c_str());
    if (onNoteSelected) onNoteSelected(selectedNote);
    repaint();
}

void PianoRollComponent::clearNoteSelection()
{
    selectedNote.clear();
    selectedNotes.clear();
    if (onNoteSelected) onNoteSelected({});
    repaint();
}

void PianoRollComponent::setSelectedNoteIds(const std::vector<juce::String>& noteIds)
{
    selectedNotes.clear();
    for (const auto& id : noteIds)
        if (id.isNotEmpty()) selectedNotes.insert(id.toStdString());
    selectedNote = noteIds.empty() ? juce::String{} : noteIds.front();
    if (onNoteSelected) onNoteSelected(selectedNote);
    repaint();
}

std::vector<juce::String> PianoRollComponent::selectedNoteIds() const
{
    std::vector<juce::String> result;
    result.reserve(selectedNotes.size() + (selectedNote.isNotEmpty() ? 1u : 0u));
    for (const auto& id : selectedNotes)
        result.push_back(juce::String::fromUTF8(id.c_str()));
    if (result.empty() && selectedNote.isNotEmpty()) result.push_back(selectedNote);
    return result;
}

double PianoRollComponent::gridSeconds() const
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

int PianoRollComponent::pixelForSeconds(double seconds) const
{
    return static_cast<int>(std::round(timeToX(seconds)));
}

double PianoRollComponent::secondsForPixel(int pixel) const
{
    return std::max(0.0, static_cast<double>(pixel - 58) / pixelsPerSecond);
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
    std::unordered_set<std::string> validNotes;
    for (const auto& track : snapshot.tracks)
        for (const auto& clip : track.clips)
            for (const auto& note : clip.notes) validNotes.insert(note.id.toStdString());
    std::erase_if(selectedNotes, [&](const auto& id) { return !validNotes.contains(id); });
    if (selectedNote.isNotEmpty() && !validNotes.contains(selectedNote.toStdString()))
        selectedNote.clear();
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
            thumbnail->addChangeListener(this);
            if (clip.sourceFile.existsAsFile())
                thumbnail->setSource(new juce::FileInputSource(clip.sourceFile));
            next.emplace(key, std::move(thumbnail));
        }
    thumbnails = std::move(next);
    updateCanvasSize();
    repaint();
}

void PianoRollComponent::updateCanvasSize()
{
    auto duration = snapshot.durationSeconds();
    if (sourceEditMode)
        for (const auto& track : snapshot.tracks)
            for (const auto& clip : track.clips)
                if (clip.id == focusedClip)
                    if (const auto found = thumbnails.find(clip.sourceFile.getFullPathName().toStdString());
                        found != thumbnails.end())
                        duration = std::max(duration, found->second->getTotalLength());
    setSize(static_cast<int>(timeToX(duration) + 400.0f),
            (highestMidi - lowestMidi + 1) * static_cast<int>(rowHeight));
}

void PianoRollComponent::drawClipWaveforms(juce::Graphics& g)
{
    juce::String focusedSource;
    if (sourceEditMode)
        for (const auto& track : snapshot.tracks)
            for (const auto& clip : track.clips)
                if (clip.id == focusedClip)
                    focusedSource = clip.sourceFile.getFullPathName();
    bool sourceWaveformDrawn = false;
    for (const auto& track : snapshot.tracks)
    {
        if (!track.compose && !sourceEditMode) continue;
        if (!sourceEditMode && focusedTrack.isNotEmpty() && track.id != focusedTrack) continue;
        for (const auto& clip : track.clips)
        {
            if (sourceEditMode && clip.id != focusedClip)
                continue;
            const auto found = thumbnails.find(clip.sourceFile.getFullPathName().toStdString());
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
            if (sourceEditMode && sourceWaveformDrawn) continue;
            const auto start = sourceEditMode ? 0.0 : clip.startSeconds;
            const auto sourceLength = sourceEditMode ? found->second->getTotalLength()
                                                      : (clip.sourceDurationSeconds > 1.0e-9
                                                          ? clip.sourceDurationSeconds : clip.durationSeconds);
            const auto waveformHeight = rowHeight * 10.0f;
            const juce::Rectangle<float> bounds(timeToX(start),
                midiToY(centreMidi) + rowHeight * 0.5f - waveformHeight * 0.5f,
                std::max(4.0f, static_cast<float>(sourceEditMode ? sourceLength : clip.durationSeconds)
                                      * pixelsPerSecond), waveformHeight);
            g.setColour(Palette::textMuted.withAlpha(track.muted ? 0.12f : 0.34f));
            // Stereo material uses one full-height editing waveform instead of
            // JUCE's stacked per-channel lanes.  Audio playback/export remains
            // stereo; this selects channel 1 for display only.
            found->second->drawChannel(
                g, bounds.toNearestInt(),
                sourceEditMode ? 0.0 : clip.sourceOffsetSeconds,
                sourceEditMode ? sourceLength : clip.sourceOffsetSeconds + sourceLength,
                0, 1.0f);
            if (sourceEditMode) sourceWaveformDrawn = true;
        }
    }
}

void PianoRollComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &model)
        rebuildLayout();
    else
    {
        updateCanvasSize();
        repaint();
    }
}

void PianoRollComponent::paint(juce::Graphics& g)
{
    g.fillAll(Palette::graphBackground);
    noteHits.clear();
    regionHandleHits.clear();

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

    // In wrench mode all timing edits are made against the untouched source.
    // The four handles mirror the main-branch HJM editor and intentionally sit
    // above the note hit targets so a boundary can always be grabbed.
    if (sourceEditMode)
    {
        for (int index = 0; index < static_cast<int>(sampleRegions.size()); ++index)
        {
            const auto& region = sampleRegions[static_cast<std::size_t>(index)];
            const auto selected = index == activeSampleRegion;
            const auto startX = timeToX(region.regionStartSeconds);
            const auto endX = timeToX(region.regionEndSeconds);
            const auto fixedX = timeToX(region.regionStartSeconds + region.fixedDurationSeconds);
            const auto alignmentX = timeToX(region.alignmentSeconds);
            const auto band = juce::Rectangle<float>(startX, 4.0f,
                std::max(1.0f, endX - startX), static_cast<float>(getHeight() - 8));
            g.setColour(Palette::accentLight.withAlpha(selected ? 0.105f : 0.035f));
            g.fillRect(band);

            const auto drawHandle = [&](float x, RegionHandle handle, juce::Colour colour,
                                        float thickness, bool dashed)
            {
                juce::Path path;
                path.startNewSubPath(x, 4.0f);
                path.lineTo(x, static_cast<float>(getHeight() - 4));
                g.setColour(colour.withAlpha(selected ? 0.94f : 0.48f));
                if (dashed)
                {
                    const float dashes[] { 5.0f, 4.0f };
                    juce::Path dashedPath;
                    juce::PathStrokeType(thickness).createDashedStroke(dashedPath, path, dashes, 2);
                    g.fillPath(dashedPath);
                }
                else g.strokePath(path, juce::PathStrokeType(thickness));
                regionHandleHits.push_back({ index, handle, x });
            };
            drawHandle(startX, RegionHandle::start, Palette::accentLight, selected ? 2.2f : 1.0f, false);
            if (region.fixedDurationSeconds > 0.0)
                drawHandle(fixedX, RegionHandle::fixedEnd, Palette::noteLight,
                           selected ? 2.0f : 1.0f, true);
            drawHandle(alignmentX, RegionHandle::alignment, Palette::noteFill,
                       selected ? 2.4f : 1.2f, false);
            drawHandle(endX, RegionHandle::end, Palette::accentLight,
                       selected ? 2.2f : 1.0f, false);

            if (selected)
            {
                const auto label = region.name.isEmpty() ? "region " + juce::String(index + 1)
                                                         : region.name;
                const auto labelBounds = juce::Rectangle<float>(startX + 3.0f, 6.0f,
                    std::max(40.0f, endX - startX - 6.0f), 18.0f);
                g.setColour(Palette::panelRaised.withAlpha(0.88f));
                g.fillRoundedRectangle(labelBounds, 3.0f);
                g.setColour(Palette::text);
                g.setFont(11.0f);
                g.drawFittedText(label, labelBounds.toNearestInt().reduced(4, 0),
                                 juce::Justification::centredLeft, 1);
            }
        }
    }

    static const std::array<juce::Colour, 5> contourColours {
        Palette::accentLight, Palette::accent, Palette::noteFill,
        juce::Colour(0xff45b8aa), juce::Colour(0xffad7ad6)
    };
    struct PositionedNote
    {
        juce::String id;
        int midiRow = 0;
        double start = 0.0;
        double end = 0.0;
    };
    std::vector<PositionedNote> visibleNotes;
    for (const auto& track : snapshot.tracks)
    {
        if (!track.compose && !sourceEditMode) continue;
        if (!sourceEditMode && focusedTrack.isNotEmpty() && track.id != focusedTrack) continue;
        for (const auto& clip : track.clips)
        {
            if (sourceEditMode && clip.id != focusedClip) continue;
            const auto sourceScale = sourceEditMode && clip.durationSeconds > 1.0e-9
                ? (clip.sourceDurationSeconds > 1.0e-9 ? clip.sourceDurationSeconds : clip.durationSeconds)
                    / clip.durationSeconds : 1.0;
            for (const auto& note : clip.notes)
            {
                const auto start = sourceEditMode
                    ? clip.sourceOffsetSeconds + note.startSeconds * sourceScale
                    : clip.startSeconds + note.startSeconds;
                visibleNotes.push_back({ note.id, static_cast<int>(std::lround(note.midiNote)),
                                         start, start + note.durationSeconds * sourceScale });
            }
        }
    }
    std::stable_sort(visibleNotes.begin(), visibleNotes.end(), [](const auto& left, const auto& right)
    {
        if (left.midiRow != right.midiRow) return left.midiRow < right.midiRow;
        if (std::abs(left.start - right.start) > 1.0e-9) return left.start < right.start;
        return left.end < right.end;
    });
    std::unordered_map<std::string, int> noteLanes;
    std::unordered_map<int, std::vector<double>> laneEnds;
    std::unordered_map<int, int> laneCounts;
    for (const auto& positioned : visibleNotes)
    {
        auto& ends = laneEnds[positioned.midiRow];
        auto lane = 0;
        while (lane < static_cast<int>(ends.size())
               && ends[static_cast<std::size_t>(lane)] > positioned.start + 1.0e-7)
            ++lane;
        if (lane == static_cast<int>(ends.size())) ends.push_back(positioned.end);
        else ends[static_cast<std::size_t>(lane)] = positioned.end;
        noteLanes[positioned.id.toStdString()] = lane;
        laneCounts[positioned.midiRow] = std::max(laneCounts[positioned.midiRow], lane + 1);
    }
    std::size_t trackIndex = 0;
    for (const auto& track : snapshot.tracks)
    {
        if (!track.compose && !sourceEditMode) { ++trackIndex; continue; }
        if (!sourceEditMode && focusedTrack.isNotEmpty() && track.id != focusedTrack)
        {
            ++trackIndex;
            continue;
        }
        const auto originalColour = contourColours[trackIndex % contourColours.size()];
        for (const auto& clip : track.clips)
        {
            if (sourceEditMode && clip.id != focusedClip)
                continue;
            const auto sourceScale = sourceEditMode && clip.durationSeconds > 1.0e-9
                ? (clip.sourceDurationSeconds > 1.0e-9 ? clip.sourceDurationSeconds : clip.durationSeconds)
                    / clip.durationSeconds
                : 1.0;
            for (const auto& note : clip.notes)
            {
                const auto absoluteStart = sourceEditMode ? clip.sourceOffsetSeconds + note.startSeconds * sourceScale
                                                         : clip.startSeconds + note.startSeconds;
                const auto x = timeToX(absoluteStart);
                const auto y = midiToY(note.midiNote);
                const auto width = std::max(5.0f, static_cast<float>(note.durationSeconds * sourceScale)
                                                     * pixelsPerSecond);
                const auto midiRow = static_cast<int>(std::lround(note.midiNote));
                const auto laneCount = std::max(1, laneCounts[midiRow]);
                const auto lane = noteLanes[note.id.toStdString()];
                const auto laneHeight = std::max(4.0f, (rowHeight - 4.0f) / laneCount);
                const auto bounds = juce::Rectangle<float>(x, y + 2.0f + lane * laneHeight,
                    width, std::max(3.0f, laneHeight - 1.0f));
                noteHits.push_back({ note.id, bounds, note.midiNote, note.startSeconds,
                                     note.durationSeconds, clip.startSeconds });

                g.setColour(Palette::noteFill.darker(0.18f));
                g.fillRoundedRectangle(bounds, 4.0f);
                const auto consonantWidth = juce::jlimit(0.0f, width,
                    static_cast<float>(note.consonantSeconds * sourceScale) * pixelsPerSecond);
                g.setColour(Palette::noteLight.withAlpha(0.58f));
                g.fillRoundedRectangle(bounds.withWidth(consonantWidth), 4.0f);
                g.setColour(Palette::noteEdge);
                g.drawRoundedRectangle(bounds, 4.0f, 1.2f);
                if (selectedNotes.contains(note.id.toStdString()))
                {
                    g.setColour(Palette::text.withAlpha(0.95f));
                    g.drawRoundedRectangle(bounds.reduced(1.0f), 3.0f, 1.8f);
                }

                if (showNoteLabels && note.label.isNotEmpty() && bounds.getWidth() >= 18.0f)
                {
                    g.setColour(Palette::text.withAlpha(0.86f));
                    g.setFont(std::min(11.0f, std::max(8.0f, bounds.getHeight() - 2.0f)));
                    g.drawFittedText(note.label, bounds.toNearestInt().reduced(4, 0),
                                     juce::Justification::centredLeft, 1);
                }

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
                    const auto markerX = x + static_cast<float>(marker * sourceScale) * pixelsPerSecond;
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
                    const auto px = x + static_cast<float>(point.timeSeconds * sourceScale) * pixelsPerSecond;
                    const auto sourceCenter = note.sourceMidiCenter >= 0.0f
                        ? note.sourceMidiCenter : note.midiNote;
                    const auto originalPitch = sourceCenter + point.relativeCents / 100.0f;
                    const auto pitch = note.midiNote + renderedPitchCents(note, point) / 100.0f;
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

    if (draggedNote.isNotEmpty() && dragMode == DragMode::pitch)
    {
        g.setColour(Palette::noteLight.withAlpha(0.8f));
        g.drawHorizontalLine(static_cast<int>(midiToY(previewMidi) + rowHeight * 0.5f),
                             58.0f, static_cast<float>(getWidth()));
    }

    if (!pitchStroke.empty()
        && (dragMode == DragMode::drawPitch || dragMode == DragMode::linePitch))
    {
        juce::Path preview;
        for (std::size_t index = 0; index < pitchStroke.size(); ++index)
        {
            const auto x = timeToX(pitchEditAbsoluteStart
                                   + pitchStroke[index].timeSeconds);
            const auto y = midiToY(pitchStroke[index].targetMidi) + rowHeight * 0.5f;
            if (index == 0) preview.startNewSubPath(x, y);
            else preview.lineTo(x, y);
        }
        g.setColour(Palette::noteLight.withAlpha(0.96f));
        g.strokePath(preview, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved));
    }

    if (dragMode == DragMode::marquee)
    {
        const auto selection = juce::Rectangle<float>(marqueeStart, marqueeCurrent);
        g.setColour(Palette::accent.withAlpha(0.12f));
        g.fillRect(selection);
        g.setColour(Palette::accentLight.withAlpha(0.92f));
        g.drawRect(selection, 1.0f);
    }

    g.setColour(juce::Colours::red.withAlpha(0.9f));
    g.drawVerticalLine(static_cast<int>(timeToX(playheadSeconds)), 0.0f, static_cast<float>(getHeight()));
    drawKeyboard();
}

void PianoRollComponent::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    if (const auto* viewport = findParentComponentOfClass<juce::Viewport>())
        if (event.x < viewport->getViewPositionX() + 58)
            return;
    if (sourceEditMode)
    {
        const RegionHandleHit* closest = nullptr;
        auto closestDistance = 7.0f;
        // Reverse order gives the active/later region priority at shared edges.
        for (auto it = regionHandleHits.rbegin(); it != regionHandleHits.rend(); ++it)
        {
            const auto distance = std::abs(event.position.x - it->x);
            if (distance <= closestDistance)
            {
                closest = &*it;
                closestDistance = distance;
            }
        }
        if (closest != nullptr)
        {
            draggedSampleRegion = closest->region;
            draggedRegionHandle = closest->handle;
            activeSampleRegion = closest->region;
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            if (onSampleRegionEdited)
                onSampleRegionEdited(activeSampleRegion,
                    sampleRegions[static_cast<std::size_t>(activeSampleRegion)], false);
            repaint();
            return;
        }
    }
    for (auto it = noteHits.rbegin(); it != noteHits.rend(); ++it)
        if (it->bounds.contains(event.position))
        {
            selectedNote = it->id;
            const auto id = selectedNote.toStdString();
            if (event.mods.isCommandDown())
            {
                if (selectedNotes.contains(id)) selectedNotes.erase(id);
                else selectedNotes.insert(id);
                if (!selectedNotes.contains(id))
                    selectedNote = selectedNotes.empty() ? juce::String()
                        : juce::String::fromUTF8(selectedNotes.begin()->c_str());
            }
            else if (event.mods.isShiftDown())
            {
                selectedNotes.insert(id);
            }
            else
            {
                if (!selectedNotes.contains(id))
                {
                    selectedNotes.clear();
                    selectedNotes.insert(id);
                }
            }
            if (onNoteSelected) onNoteSelected(selectedNote);
            if (selectedNote.isEmpty()) { repaint(); return; }
            if (tool == Tool::connect && !sourceEditMode)
            {
                model.toggleNoteConnection(selectedNote);
                return;
            }
            draggedNote = it->id;
            if (!sourceEditMode && (tool == Tool::draw || tool == Tool::line))
            {
                dragMode = tool == Tool::draw ? DragMode::drawPitch : DragMode::linePitch;
                pitchEditAbsoluteStart = static_cast<double>(it->bounds.getX() - 58.0f)
                    / pixelsPerSecond;
                const auto local = juce::jlimit(0.0, it->durationSeconds,
                    static_cast<double>(event.position.x - it->bounds.getX()) / pixelsPerSecond);
                pitchStroke = { { local,
                    juce::jlimit(0.0f, 127.0f, yToMidi(event.position.y)) } };
                repaint();
                return;
            }
            dragStartMidi = it->midi;
            previewMidi = dragStartMidi;
            dragStartY = event.position.y;
            finePitchDrag = event.mods.isAltDown();
            dragStartSeconds = it->startSeconds;
            dragDurationSeconds = it->durationSeconds;
            dragClipStartSeconds = it->clipStartSeconds;
            previewStartSeconds = dragStartSeconds;
            previewDurationSeconds = dragDurationSeconds;
            if (!sourceEditMode && event.position.x <= it->bounds.getX() + 6.0f)
                dragMode = DragMode::resizeLeft;
            else if (!sourceEditMode && event.position.x >= it->bounds.getRight() - 6.0f)
                dragMode = DragMode::resizeRight;
            else
                dragMode = DragMode::pitch;
            repaint();
            return;
        }
    if ((tool == Tool::draw || tool == Tool::line) && !sourceEditMode)
    {
        const auto rawSeconds = std::max(0.0,
            static_cast<double>(event.position.x - 58.0f) / pixelsPerSecond);
        const auto step = gridSeconds();
        const auto relative = rawSeconds - snapshot.beatOriginSeconds;
        const auto quantized = snapshot.beatOriginSeconds + std::round(relative / step) * step;
        const auto id = model.addNote(focusedClip, std::max(0.0, quantized), step,
                                      juce::jlimit(0.0f, 127.0f, std::round(yToMidi(event.position.y))));
        if (id.isNotEmpty())
        {
            selectedNote = id;
            selectedNotes.clear();
            selectedNotes.insert(id.toStdString());
            if (onNoteSelected) onNoteSelected(id);
        }
        return;
    }
    marqueeAddsToSelection = event.mods.isShiftDown() || event.mods.isCommandDown();
    if (!marqueeAddsToSelection)
    {
        selectedNote.clear();
        selectedNotes.clear();
        if (onNoteSelected) onNoteSelected({});
    }
    if (!sourceEditMode && tool == Tool::note)
    {
        marqueeStart = event.position;
        marqueeCurrent = event.position;
        dragMode = DragMode::marquee;
    }
    repaint();
    if (onSeek)
        onSeek(std::max(0.0, static_cast<double>(event.position.x - 58.0f) / pixelsPerSecond));
}

void PianoRollComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (sourceEditMode || tool == Tool::draw || tool == Tool::line) return;
    for (auto it = noteHits.rbegin(); it != noteHits.rend(); ++it)
    {
        if (!it->bounds.contains(event.position)) continue;
        for (const auto& track : snapshot.tracks)
            for (const auto& clip : track.clips)
                for (const auto& note : clip.notes)
                    if (note.id == it->id)
                    {
                        draggedNote.clear();
                        dragMode = DragMode::none;
                        const auto local = juce::jlimit(0.0, note.durationSeconds,
                            static_cast<double>(event.position.x - it->bounds.getX())
                                / pixelsPerSecond);
                        if (tool == Tool::connect)
                        {
                            const auto created = model.splitNote(note.id, local);
                            if (created.isNotEmpty())
                            {
                                selectedNote = created;
                                selectedNotes.clear();
                                selectedNotes.insert(created.toStdString());
                                if (onNoteSelected) onNoteSelected(created);
                            }
                            return;
                        }
                        const auto noteStart = clip.startSeconds + note.startSeconds;
                        const auto boundary = noteStart + note.consonantSeconds;
                        const auto boundaryX = timeToX(boundary);
                        if (std::abs(event.position.x - boundaryX) <= 7.0f)
                        {
                            const auto step = gridSeconds();
                            const auto quantized = snapshot.beatOriginSeconds
                                + std::round((boundary - snapshot.beatOriginSeconds) / step) * step;
                            model.setNoteAttack(note.id,
                                juce::jlimit(0.0, note.durationSeconds, quantized - noteStart),
                                note.attackSpeed);
                        }
                        else
                            model.transposeNotes({ note.id }, std::round(note.midiNote) - note.midiNote);
                        return;
                    }
        return;
    }
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (draggedSampleRegion >= 0
        && draggedSampleRegion < static_cast<int>(sampleRegions.size()))
    {
        auto& region = sampleRegions[static_cast<std::size_t>(draggedSampleRegion)];
        const auto seconds = std::max(0.0,
            static_cast<double>(event.position.x - 58.0f) / pixelsPerSecond);
        constexpr double minimum = 0.001;
        switch (draggedRegionHandle)
        {
            case RegionHandle::start:
            {
                const auto next = std::min(region.regionEndSeconds - minimum, seconds);
                const auto fixedEnd = region.regionStartSeconds + region.fixedDurationSeconds;
                region.regionStartSeconds = next;
                region.fixedDurationSeconds = juce::jlimit(0.0,
                    region.regionEndSeconds - region.regionStartSeconds,
                    fixedEnd - region.regionStartSeconds);
                region.alignmentSeconds = juce::jlimit(region.regionStartSeconds,
                    region.regionEndSeconds, region.alignmentSeconds);
                break;
            }
            case RegionHandle::fixedEnd:
                region.fixedDurationSeconds = juce::jlimit(0.0,
                    region.regionEndSeconds - region.regionStartSeconds,
                    seconds - region.regionStartSeconds);
                break;
            case RegionHandle::alignment:
                region.alignmentSeconds = juce::jlimit(region.regionStartSeconds,
                    region.regionEndSeconds, seconds);
                break;
            case RegionHandle::end:
                region.regionEndSeconds = std::max(region.regionStartSeconds + minimum, seconds);
                region.fixedDurationSeconds = std::min(region.fixedDurationSeconds,
                    region.regionEndSeconds - region.regionStartSeconds);
                region.alignmentSeconds = std::min(region.alignmentSeconds, region.regionEndSeconds);
                break;
            case RegionHandle::none: break;
        }
        if (onSampleRegionEdited) onSampleRegionEdited(draggedSampleRegion, region, false);
        repaint();
        return;
    }
    if (dragMode == DragMode::marquee)
    {
        marqueeCurrent = event.position;
        repaint();
        return;
    }
    if (draggedNote.isEmpty()) return;
    if (tool == Tool::connect) return;
    if (dragMode == DragMode::drawPitch || dragMode == DragMode::linePitch)
    {
        const auto local = std::max(0.0,
            static_cast<double>(event.position.x - 58.0f) / pixelsPerSecond
                - pitchEditAbsoluteStart);
        PitchCurveEditPoint point { local,
            juce::jlimit(0.0f, 127.0f, yToMidi(event.position.y)) };
        if (dragMode == DragMode::linePitch)
        {
            if (pitchStroke.size() == 1) pitchStroke.push_back(point);
            else pitchStroke.back() = point;
        }
        else if (pitchStroke.empty()
                 || std::abs(pitchStroke.back().timeSeconds - point.timeSeconds) >= 0.001
                 || std::abs(pitchStroke.back().targetMidi - point.targetMidi) >= 0.02f)
            pitchStroke.push_back(point);
        repaint();
        return;
    }
    if (dragMode == DragMode::pitch)
    {
        finePitchDrag = finePitchDrag || event.mods.isAltDown();
        previewMidi = finePitchDrag
            ? juce::jlimit(0.0f, 127.0f,
                dragStartMidi - (event.position.y - dragStartY) / rowHeight)
            : juce::jlimit(0.0f, 127.0f, std::round(yToMidi(event.position.y)));
    }
    else if (dragMode == DragMode::resizeLeft)
    {
        const auto end = dragStartSeconds + dragDurationSeconds;
        auto next = dragStartSeconds
            + static_cast<double>(event.getDistanceFromDragStartX()) / pixelsPerSecond;
        if (!event.mods.isAltDown())
        {
            const auto absolute = dragClipStartSeconds + next;
            next = snapshot.beatOriginSeconds
                + std::round((absolute - snapshot.beatOriginSeconds) / gridSeconds()) * gridSeconds()
                - dragClipStartSeconds;
        }
        previewStartSeconds = juce::jlimit(0.0, end - 0.01, next);
        previewDurationSeconds = end - previewStartSeconds;
    }
    else if (dragMode == DragMode::resizeRight)
    {
        auto end = dragClipStartSeconds + dragStartSeconds + dragDurationSeconds
            + static_cast<double>(event.getDistanceFromDragStartX()) / pixelsPerSecond;
        if (!event.mods.isAltDown())
            end = snapshot.beatOriginSeconds
                + std::round((end - snapshot.beatOriginSeconds) / gridSeconds()) * gridSeconds();
        previewDurationSeconds = std::max(0.01,
            end - dragClipStartSeconds - dragStartSeconds);
    }
    repaint();
}

void PianoRollComponent::mouseUp(const juce::MouseEvent&)
{
    if (draggedSampleRegion >= 0
        && draggedSampleRegion < static_cast<int>(sampleRegions.size()))
    {
        if (onSampleRegionEdited)
            onSampleRegionEdited(draggedSampleRegion,
                sampleRegions[static_cast<std::size_t>(draggedSampleRegion)], true);
        draggedSampleRegion = -1;
        draggedRegionHandle = RegionHandle::none;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    if (dragMode == DragMode::marquee)
    {
        const auto selection = juce::Rectangle<float>(marqueeStart, marqueeCurrent);
        if (!marqueeAddsToSelection) selectedNotes.clear();
        for (const auto& hit : noteHits)
            if (selection.intersects(hit.bounds)) selectedNotes.insert(hit.id.toStdString());
        selectedNote = selectedNotes.empty() ? juce::String()
            : juce::String::fromUTF8(selectedNotes.begin()->c_str());
        dragMode = DragMode::none;
        if (onNoteSelected) onNoteSelected(selectedNote);
        repaint();
        return;
    }
    if (draggedNote.isEmpty()) return;
    if (dragMode == DragMode::drawPitch || dragMode == DragMode::linePitch)
        model.setNotePitchCurve(draggedNote, std::move(pitchStroke));
    else if (dragMode == DragMode::pitch)
    {
        std::vector<juce::String> ids;
        ids.reserve(selectedNotes.size());
        for (const auto& id : selectedNotes) ids.push_back(juce::String::fromUTF8(id.c_str()));
        if (ids.empty()) ids.push_back(draggedNote);
        model.transposeNotes(ids, previewMidi - dragStartMidi);
    }
    else
        model.resizeNote(draggedNote, previewStartSeconds, previewDurationSeconds);
    draggedNote.clear();
    finePitchDrag = false;
    pitchStroke.clear();
    dragMode = DragMode::none;
}

bool PianoRollComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'A')
    {
        selectAllNotes();
        return true;
    }
    if ((key.getKeyCode() == juce::KeyPress::deleteKey
         || key.getKeyCode() == juce::KeyPress::backspaceKey)
        && (selectedNote.isNotEmpty() || !selectedNotes.empty()))
    {
        std::vector<juce::String> removed;
        removed.reserve(selectedNotes.size());
        for (const auto& id : selectedNotes) removed.push_back(juce::String::fromUTF8(id.c_str()));
        if (removed.empty()) removed.push_back(selectedNote);
        selectedNote.clear();
        selectedNotes.clear();
        model.removeNotes(removed);
        if (onNoteSelected) onNoteSelected({});
        return true;
    }
    return false;
}
}
