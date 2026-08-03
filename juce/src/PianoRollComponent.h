#pragma once

#include "ProjectModel.h"
#include "SampleSettings.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace hachi
{
class PianoRollComponent final : public juce::Component,
                                 private juce::ChangeListener
{
public:
    enum class Tool { note, draw, line, connect };

    explicit PianoRollComponent(ProjectModel& modelToUse);
    ~PianoRollComponent() override;

    void setPixelsPerSecond(float value);
    void setSourceEditMode(bool enabled);
    void setFocusedClip(const juce::String& clipId);
    void setSampleRegions(const std::vector<SampleRegionSetting>& regions, int activeRegion);
    void setPlayheadSeconds(double seconds);
    void setTool(Tool nextTool);
    void selectAllNotes();
    void clearNoteSelection();
    [[nodiscard]] std::vector<juce::String> selectedNoteIds() const;
    [[nodiscard]] int pixelForSeconds(double seconds) const;
    [[nodiscard]] double secondsForPixel(int pixel) const;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    std::function<void(double)> onSeek;
    std::function<void(const juce::String&)> onNoteSelected;
    std::function<void(int, const SampleRegionSetting&, bool)> onSampleRegionEdited;

private:
    struct NoteHit
    {
        juce::String id;
        juce::Rectangle<float> bounds;
        float midi = 60.0f;
        double startSeconds = 0.0;
        double durationSeconds = 0.0;
    };

    enum class RegionHandle { none, start, fixedEnd, alignment, end };
    struct RegionHandleHit
    {
        int region = -1;
        RegionHandle handle = RegionHandle::none;
        float x = 0.0f;
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void rebuildLayout();
    void updateCanvasSize();
    void drawClipWaveforms(juce::Graphics& g);
    [[nodiscard]] float timeToX(double seconds) const;
    [[nodiscard]] float midiToY(float midi) const;
    [[nodiscard]] float yToMidi(float y) const;
    [[nodiscard]] double gridSeconds() const;

    ProjectModel& model;
    ProjectData snapshot;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbnailCache { 96 };
    std::unordered_map<std::string, std::unique_ptr<juce::AudioThumbnail>> thumbnails;
    std::vector<NoteHit> noteHits;
    std::vector<SampleRegionSetting> sampleRegions;
    std::vector<RegionHandleHit> regionHandleHits;
    int activeSampleRegion = -1;
    int draggedSampleRegion = -1;
    RegionHandle draggedRegionHandle = RegionHandle::none;
    float pixelsPerSecond = 140.0f;
    float rowHeight = 18.0f;
    int highestMidi = 96;
    int lowestMidi = 24;
    bool sourceEditMode = false;
    Tool tool = Tool::note;
    juce::String focusedClip;
    double playheadSeconds = 0.0;
    juce::String selectedNote;
    std::unordered_set<std::string> selectedNotes;
    juce::String draggedNote;
    float dragStartMidi = 0.0f;
    float previewMidi = 0.0f;
    enum class DragMode { none, pitch, resizeLeft, resizeRight, drawPitch, linePitch } dragMode = DragMode::none;
    double dragStartSeconds = 0.0;
    double dragDurationSeconds = 0.0;
    double previewStartSeconds = 0.0;
    double previewDurationSeconds = 0.0;
    double pitchEditAbsoluteStart = 0.0;
    std::vector<PitchCurveEditPoint> pitchStroke;
};
}
