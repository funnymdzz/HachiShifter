#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

namespace hachi
{
enum class PitchAlgorithm
{
    mld5,
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
    float attackSpeed = 1.0f;
    bool connectedToPrevious = false;
    bool connectedToNext = false;
    std::vector<PitchPoint> contour;
    std::vector<double> sibilantMarkers;
};

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

    void addAudioFile(const juce::File& file, double durationSeconds, double startSeconds = 0.0);
    bool addMidiFile(const juce::File& file, juce::String& error);
    void setTempo(double bpm, int numerator, int denominator = 4);
    void setGridDivision(const juce::String& division);
    void setBaseScale(const juce::String& scale);
    void setTrackCompose(const juce::String& trackId, bool enabled);
    void setTrackMuted(const juce::String& trackId, bool muted);
    void setTrackSolo(const juce::String& trackId, bool solo);
    void setTrackVolume(const juce::String& trackId, float volume);
    void setTrackPan(const juce::String& trackId, float pan);
    void setPitchAlgorithm(PitchAlgorithm algorithm);
    void setStretchAlgorithm(StretchAlgorithm algorithm);
    void moveClip(const juce::String& clipId, double startSeconds);
    void transposeNote(const juce::String& noteId, float semitones);
    void resizeNote(const juce::String& noteId, double newStart, double newDuration);
    [[nodiscard]] juce::String addNote(const juce::String& preferredClipId,
                                       double absoluteStart, double duration, float midiNote);
    void removeNote(const juce::String& noteId);
    void toggleNoteConnection(const juce::String& noteId);

    bool save(const juce::File& file, juce::String& error) const;
    bool load(const juce::File& file, juce::String& error);

private:
    juce::ValueTree toValueTree() const;
    static ProjectData fromValueTree(const juce::ValueTree& tree);
    static juce::String makeId(const char* prefix);

    mutable juce::CriticalSection lock;
    ProjectData project;
};
}
