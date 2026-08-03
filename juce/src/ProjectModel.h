#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

namespace hachi
{
struct SampleRegionSetting;
enum class PitchAlgorithm
{
    mld5,
    mld3,
    nsfHifigan,
    world,
    vocalShifter
};

enum class StretchAlgorithm
{
    melodyneHybrid,
    variableMelHop,
    loop,
    soundTouch
};

struct PitchPoint
{
    double timeSeconds = 0.0;
    float relativeCents = 0.0f;
    float withoutVibratoCents = 0.0f;
    bool voiced = true;
    float manualTargetCents = 0.0f;
    bool hasManualTarget = false;
};

struct PitchCurveEditPoint
{
    double timeSeconds = 0.0;
    float targetMidi = 60.0f;
};

// A source-time anchor imported from an external editor.  Both values are
// relative to the beginning of the clip: targetSeconds is the position on the
// HachiShifter timeline and sourceSeconds is the corresponding position in the
// selected source-media range.  Keeping these anchors in the native project is
// essential for preserving nonlinear Attack/vowel timing instead of reducing
// every imported element to one linear duration ratio.
struct SourceTimePoint
{
    double targetSeconds = 0.0;
    double sourceSeconds = 0.0;
};

struct NoteData
{
    juce::String id;
    double startSeconds = 0.0;
    double durationSeconds = 0.25;
    double consonantSeconds = 0.04;
    float midiNote = 60.0f;
    float sourceMidiCenter = -1.0f;
    float modulation = 1.0f;
    float drift = 1.0f;
    float tension = 0.0f;
    float breath = 0.0f;
    float formantSemitones = 0.0f;
    float gain = 1.0f;
    float attackSpeed = 1.0f;
    // Melodyne's Robust Pitch Curve is stored by the detector/source.  Hachi
    // exposes the same behaviour per note so difficult notes can opt in
    // without changing the rest of the source analysis.
    bool robustPitchCurve = false;
    bool connectedToPrevious = false;
    bool connectedToNext = false;
    std::vector<PitchPoint> contour;
    std::vector<double> sibilantMarkers;
};

[[nodiscard]] float renderedPitchCents(const NoteData& note, const PitchPoint& point);

struct ClipData
{
    juce::String id;
    juce::File sourceFile;
    double startSeconds = 0.0;
    double sourceOffsetSeconds = 0.0;
    double sourceDurationSeconds = 0.0;
    double durationSeconds = 1.0;
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    float gain = 1.0f;
    bool muted = false;
    std::vector<SourceTimePoint> sourceTimeMap;
    std::vector<NoteData> notes;
};

struct TrackData
{
    juce::String id;
    juce::String name;
    bool compose = true;
    bool muted = false;
    bool solo = false;
    float volume = 1.0f;
    float pan = 0.0f;
    PitchAlgorithm pitchAlgorithm = PitchAlgorithm::mld5;
    StretchAlgorithm stretchAlgorithm = StretchAlgorithm::melodyneHybrid;
    std::vector<ClipData> clips;
};

struct ProjectData
{
    juce::String name = "Untitled";
    double bpm = 120.0;
    double beatOriginSeconds = 0.0;
    int numerator = 4;
    int denominator = 4;
    juce::String gridDivision = "1/16";
    juce::String baseScale = "C";
    std::vector<TrackData> tracks;

    [[nodiscard]] double durationSeconds() const;
};

class ProjectModel final : public juce::ChangeBroadcaster
{
public:
    ProjectModel();

    [[nodiscard]] ProjectData snapshot() const;
    void replace(ProjectData replacement);
    void clear();
    bool undo();
    bool redo();
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;

    [[nodiscard]] juce::String addAudioFile(const juce::File& file, double durationSeconds,
                                            double startSeconds = 0.0,
                                            const juce::String& targetTrackId = {});
    [[nodiscard]] juce::String addTrack(const juce::String& name, bool compose = true);
    void setTrackName(const juce::String& trackId, const juce::String& name);
    bool setClipNotesIfEmpty(const juce::String& clipId, std::vector<NoteData> notes);
    bool addMidiFile(const juce::File& file, juce::String& error);
    void setTempo(double bpm, int numerator, int denominator = 4);
    void setGridDivision(const juce::String& division);
    void setBaseScale(const juce::String& scale);
    void setTrackCompose(const juce::String& trackId, bool enabled);
    void setTrackMuted(const juce::String& trackId, bool muted);
    void setTrackSolo(const juce::String& trackId, bool solo);
    void setTrackVolume(const juce::String& trackId, float volume);
    void setTrackPan(const juce::String& trackId, float pan);
    void setTrackPitchAlgorithm(const juce::String& trackId, PitchAlgorithm algorithm);
    void setTrackStretchAlgorithm(const juce::String& trackId, StretchAlgorithm algorithm);
    void setPitchAlgorithm(PitchAlgorithm algorithm);
    void setStretchAlgorithm(StretchAlgorithm algorithm);
    void moveClip(const juce::String& clipId, double startSeconds);
    [[nodiscard]] juce::String duplicateClip(const juce::String& clipId,
                                             double startSeconds = -1.0,
                                             const juce::String& targetTrackId = {});
    void resizeClip(const juce::String& clipId, double startSeconds,
                    double durationSeconds);
    void setClipGain(const juce::String& clipId, float gain);
    void setClipFades(const juce::String& clipId, double fadeInSeconds,
                      double fadeOutSeconds);
    void setClipMuted(const juce::String& clipId, bool muted);
    void removeClip(const juce::String& clipId);
    void removeTrack(const juce::String& trackId);
    void transposeNote(const juce::String& noteId, float semitones);
    void transposeNotes(const std::vector<juce::String>& noteIds, float semitones);
    void setNotesMidi(const std::vector<juce::String>& noteIds, float midiNote);
    void averageNotesMidi(const std::vector<juce::String>& noteIds);
    void quantizeNotesMidi(const std::vector<juce::String>& noteIds, float stepSemitones = 1.0f);
    void resizeNote(const juce::String& noteId, double newStart, double newDuration);
    void setNoteModulation(const juce::String& noteId, float modulation);
    void setNoteDrift(const juce::String& noteId, float drift);
    void setNoteTension(const juce::String& noteId, float tension);
    void setNoteBreath(const juce::String& noteId, float breath);
    void setNoteFormant(const juce::String& noteId, float semitones);
    void setNoteGain(const juce::String& noteId, float gain);
    void setNoteAttack(const juce::String& noteId, double consonantSeconds, float attackSpeed);
    void setNoteAttackSpeed(const juce::String& noteId, float attackSpeed);
    void setNoteRobustPitchCurve(const juce::String& noteId, bool enabled);
    bool setNotePitchCurve(const juce::String& noteId,
                           std::vector<PitchCurveEditPoint> points);
    [[nodiscard]] juce::String addNote(const juce::String& preferredClipId,
                                       double absoluteStart, double duration, float midiNote);
    void removeNote(const juce::String& noteId);
    void removeNotes(const std::vector<juce::String>& noteIds);
    void toggleNoteConnection(const juce::String& noteId);
    void applySourceSettings(const juce::File& source,
                             const std::vector<SampleRegionSetting>& rows);

    bool save(const juce::File& file, juce::String& error) const;
    bool load(const juce::File& file, juce::String& error);

private:
    void pushUndoLocked();
    juce::ValueTree toValueTree(const juce::File& projectFile) const;
    static ProjectData fromValueTree(const juce::ValueTree& tree,
                                     const juce::File& projectFile);
    static juce::String makeId(const char* prefix);

    mutable juce::CriticalSection lock;
    ProjectData project;
    std::vector<ProjectData> undoHistory;
    std::vector<ProjectData> redoHistory;
    // Keep large Melodyne projects bounded: snapshots are full native project
    // states, so a short history is preferable to unbounded contour copies.
    static constexpr std::size_t maxHistory = 24;
};
}
