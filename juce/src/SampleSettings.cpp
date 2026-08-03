#include "SampleSettings.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#elif defined(HACHI_HAS_ICONV)
 #include <iconv.h>
#endif

namespace hachi
{
namespace
{
juce::String csvEscape(const juce::String& value)
{
    if (!value.containsAnyOf(",\"\r\n")) return value;
    return "\"" + value.replace("\"", "\"\"") + "\"";
}

juce::StringArray splitCsv(const juce::String& line)
{
    juce::StringArray fields;
    juce::String current;
    auto quoted = false;
    for (int index = 0; index < line.length(); ++index)
    {
        const auto character = line[index];
        if (character == '"')
        {
            if (quoted && index + 1 < line.length() && line[index + 1] == '"')
            {
                current += '"';
                ++index;
            }
            else quoted = !quoted;
        }
        else if (character == ',' && !quoted)
        {
            fields.add(current);
            current.clear();
        }
        else current += character;
    }
    fields.add(current);
    return fields;
}

double number(const juce::StringArray& values, int index, double fallback)
{
    if (index >= values.size() || values[index].trim().isEmpty()) return fallback;
    return values[index].getDoubleValue();
}

juce::String decodeOtoText(const juce::File& file)
{
    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes) || bytes.getSize() == 0) return {};
    const auto* data = static_cast<const char*>(bytes.getData());
    auto size = bytes.getSize();
    if (size >= 3 && static_cast<unsigned char>(data[0]) == 0xef
        && static_cast<unsigned char>(data[1]) == 0xbb
        && static_cast<unsigned char>(data[2]) == 0xbf)
    {
        data += 3;
        size -= 3;
    }
    if (juce::CharPointer_UTF8::isValidString(data, static_cast<int>(size)))
        return juce::String::fromUTF8(data, static_cast<int>(size));

#if JUCE_WINDOWS
    const auto wideLength = MultiByteToWideChar(932, 0, data, static_cast<int>(size), nullptr, 0);
    if (wideLength > 0)
    {
        std::vector<wchar_t> wide(static_cast<std::size_t>(wideLength + 1), 0);
        if (MultiByteToWideChar(932, 0, data, static_cast<int>(size), wide.data(), wideLength) > 0)
            return juce::String(wide.data());
    }
#elif defined(HACHI_HAS_ICONV)
    auto decoder = iconv_open("UTF-8", "CP932");
    if (decoder == reinterpret_cast<iconv_t>(-1)) decoder = iconv_open("UTF-8", "SHIFT-JIS");
    if (decoder != reinterpret_cast<iconv_t>(-1))
    {
        std::vector<char> decoded(size * 4 + 4, 0);
        auto* input = const_cast<char*>(data);
        auto inputLeft = size;
        auto* output = decoded.data();
        auto outputLeft = decoded.size() - 1;
        const auto result = iconv(decoder, &input, &inputLeft, &output, &outputLeft);
        iconv_close(decoder);
        if (result != static_cast<std::size_t>(-1))
            return juce::String::fromUTF8(decoded.data(),
                static_cast<int>(decoded.size() - outputLeft - 1));
    }
#endif
    return juce::String::fromUTF8(data, static_cast<int>(size));
}
}

juce::File SampleSettings::sidecarFor(const juce::File& audio)
{
    // Native rewrite uses the requested interoperable name while still
    // accepting the main-branch .hachi.csv file during migration.
    return juce::File(audio.getFullPathName() + ".hjm.csv");
}

std::vector<SampleRegionSetting> SampleSettings::loadOrDerive(const juce::File& audio,
                                                              const ProjectData& project)
{
    auto sidecar = sidecarFor(audio);
    if (!sidecar.existsAsFile())
    {
        const juce::File legacy(audio.getFullPathName() + ".hachi.csv");
        if (legacy.existsAsFile()) sidecar = legacy;
    }
    std::vector<SampleRegionSetting> rows;
    if (sidecar.existsAsFile())
    {
        auto lines = juce::StringArray::fromLines(sidecar.loadFileAsString());
        for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
        {
            const auto line = lines[lineIndex].trim();
            if (line.isEmpty() || line.startsWithIgnoreCase("name,")) continue;
            const auto values = splitCsv(line);
            if (values.size() < 6) continue;
            SampleRegionSetting row;
            row.name = values[0].trim();
            row.regionStartSeconds = number(values, 1, 0.0);
            row.regionEndSeconds = number(values, 2, row.regionStartSeconds + 0.5);
            row.alignmentSeconds = number(values, 3, row.regionStartSeconds);
            row.fixedDurationSeconds = number(values, 4, 0.0);
            row.relativePitchCents = number(values, 5, 0.0);
            row.melodyneData = number(values, 6, 0.0) >= 0.5;
            row.melodynePitchCenterCents = number(values, 7, 0.0);
            row.melodyneOriginalPitchCenterCents = number(values, 8, 0.0);
            row.melodynePitchDrift = number(values, 9, 1.0);
            row.melodynePitchModulation = number(values, 10, 1.0);
            row.melodyneTransitionSeconds = number(values, 11, 0.0);
            row.melodyneFormantCents = number(values, 12, 0.0);
            row.melodyneAmplitude = number(values, 13, 1.0);
            row.melodyneSibilantBalance = number(values, 14, 0.0);
            row.melodyneAttackSeconds = number(values, 15, 0.0);
            row.melodyneDecayElongation = number(values, 16, 0.0);
            if (row.regionEndSeconds > row.regionStartSeconds) rows.push_back(std::move(row));
        }
    }
    if (!rows.empty()) return rows;

    struct Positioned { const ClipData* clip; const NoteData* note; double sourceStart; };
    std::vector<Positioned> notes;
    for (const auto& track : project.tracks)
        for (const auto& clip : track.clips)
            if (clip.sourceFile == audio)
                for (const auto& note : clip.notes)
                {
                    const auto ratio = clip.durationSeconds > 1.0e-9
                        ? clip.sourceDurationSeconds / clip.durationSeconds : 1.0;
                    notes.push_back({ &clip, &note,
                        clip.sourceOffsetSeconds + note.startSeconds * ratio });
                }
    std::stable_sort(notes.begin(), notes.end(), [](const auto& left, const auto& right)
    {
        return left.sourceStart < right.sourceStart;
    });
    // The same source may be placed many times on the timeline.  Wrench mode
    // describes the source file, so equal source regions are one HJM row rather
    // than one row per project placement.
    std::vector<Positioned> uniqueNotes;
    uniqueNotes.reserve(notes.size());
    for (const auto& item : notes)
    {
        const auto ratio = item.clip->durationSeconds > 1.0e-9
            ? item.clip->sourceDurationSeconds / item.clip->durationSeconds : 1.0;
        const auto sourceDuration = item.note->durationSeconds * ratio;
        const auto duplicate = std::any_of(uniqueNotes.begin(), uniqueNotes.end(),
            [&](const auto& existing)
            {
                const auto existingRatio = existing.clip->durationSeconds > 1.0e-9
                    ? existing.clip->sourceDurationSeconds / existing.clip->durationSeconds : 1.0;
                return std::abs(existing.sourceStart - item.sourceStart) < 0.0005
                    && std::abs(existing.note->durationSeconds * existingRatio - sourceDuration)
                        < 0.0005;
            });
        if (!duplicate) uniqueNotes.push_back(item);
    }
    notes = std::move(uniqueNotes);
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const auto& item = notes[index];
        const auto ratio = item.clip->durationSeconds > 1.0e-9
            ? item.clip->sourceDurationSeconds / item.clip->durationSeconds : 1.0;
        SampleRegionSetting row;
        row.name = item.note->label.isNotEmpty()
            ? item.note->label : "note " + juce::String(index + 1);
        row.regionStartSeconds = std::max(0.0, item.sourceStart);
        row.regionEndSeconds = std::max(row.regionStartSeconds + 0.001,
            row.regionStartSeconds + item.note->durationSeconds * ratio);
        row.fixedDurationSeconds = std::min(row.regionEndSeconds - row.regionStartSeconds,
                                             item.note->consonantSeconds * ratio);
        row.alignmentSeconds = row.regionStartSeconds + row.fixedDurationSeconds;
        row.relativePitchCents = (item.note->midiNote - item.note->sourceMidiCenter) * 100.0;
        row.melodyneData = true;
        row.melodynePitchCenterCents = item.note->midiNote * 100.0;
        row.melodyneOriginalPitchCenterCents = item.note->sourceMidiCenter * 100.0;
        row.melodynePitchDrift = item.note->drift;
        row.melodynePitchModulation = item.note->modulation;
        row.melodyneFormantCents = item.note->formantSemitones * 100.0;
        // Melodyne amplitude is note-local.  Persisting the clip gain here
        // discarded the vocal's per-note level when the HJM file was loaded.
        row.melodyneAmplitude = item.note->gain;
        row.melodyneSibilantBalance = item.note->breath;
        row.melodyneAttackSeconds = item.note->consonantSeconds;
        rows.push_back(std::move(row));
    }
    if (rows.empty()) rows.push_back({ "region 1", 0.0, 0.5, 0.0, 0.0 });
    return rows;
}

bool SampleSettings::save(const juce::File& audio,
                          const std::vector<SampleRegionSetting>& input,
                          juce::String& error)
{
    auto rows = input;
    std::stable_sort(rows.begin(), rows.end(), [](const auto& left, const auto& right)
    {
        return left.regionStartSeconds < right.regionStartSeconds;
    });
    juce::String csv = "name,region_start_sec,region_end_sec,note_alignment_sec,fixed_duration_sec,relative_pitch_cents,melodyne_project_data,melodyne_pitch_center_cents,melodyne_original_pitch_center_cents,melodyne_pitch_drift_factor,melodyne_pitch_modulation_factor,melodyne_transition_sec,melodyne_formant_offset_cents,melodyne_amplitude_factor,melodyne_sibilant_balance,melodyne_attack_duration_sec,melodyne_decay_elongation\n";
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        auto row = rows[index];
        row.regionStartSeconds = std::max(0.0, row.regionStartSeconds);
        row.regionEndSeconds = std::max(row.regionStartSeconds + 0.001, row.regionEndSeconds);
        row.alignmentSeconds = juce::jlimit(row.regionStartSeconds, row.regionEndSeconds,
                                            row.alignmentSeconds);
        row.fixedDurationSeconds = juce::jlimit(0.0, row.regionEndSeconds - row.regionStartSeconds,
                                                row.fixedDurationSeconds);
        const std::array<double, 16> values {
            row.regionStartSeconds, row.regionEndSeconds, row.alignmentSeconds,
            row.fixedDurationSeconds, row.relativePitchCents,
            row.melodyneData ? 1.0 : 0.0, row.melodynePitchCenterCents,
            row.melodyneOriginalPitchCenterCents, row.melodynePitchDrift,
            row.melodynePitchModulation, row.melodyneTransitionSeconds,
            row.melodyneFormantCents, row.melodyneAmplitude,
            row.melodyneSibilantBalance, row.melodyneAttackSeconds,
            row.melodyneDecayElongation
        };
        csv += csvEscape(row.name.isEmpty() ? "region " + juce::String(index + 1) : row.name);
        for (const auto value : values) csv += "," + juce::String(value, 9).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
        csv += "\n";
    }
    const auto sidecar = sidecarFor(audio);
    if (!sidecar.replaceWithText(csv, false, false, "\n"))
    {
        error = "Could not write " + sidecar.getFullPathName();
        return false;
    }
    return true;
}

bool SampleSettings::importOto(const juce::File& oto, const juce::File& audio,
                               double audioDuration,
                               std::vector<SampleRegionSetting>& rows,
                               juce::String& error)
{
    if (!oto.existsAsFile()) { error = "oto.ini not found"; return false; }
    rows.clear();
    for (const auto& raw : juce::StringArray::fromLines(decodeOtoText(oto)))
    {
        const auto equals = raw.indexOfChar('=');
        if (equals <= 0) continue;
        const auto wav = raw.substring(0, equals).trim();
        // An oto.ini normally describes a complete voicebank.  Wrench mode is
        // scoped to one source file, so only import rows belonging to that WAV
        // instead of accidentally applying every alias in the bank to it.
        if (audio != juce::File{}
            && !juce::File(wav.replaceCharacter('\\', '/')).getFileName()
                    .equalsIgnoreCase(audio.getFileName()))
            continue;
        auto fields = juce::StringArray::fromTokens(raw.substring(equals + 1), ",", "\"");
        if (fields.size() < 6) continue;
        const auto offset = std::max(0.0, fields[1].getDoubleValue() / 1000.0);
        const auto consonant = std::max(0.0, fields[2].getDoubleValue() / 1000.0);
        const auto cutoffMs = fields[3].getDoubleValue();
        const auto preutter = std::max(0.0, fields[4].getDoubleValue() / 1000.0);
        SampleRegionSetting row;
        row.name = fields[0].trim().isNotEmpty() ? fields[0].trim()
                                                   : juce::File(wav).getFileNameWithoutExtension();
        row.regionStartSeconds = offset;
        row.fixedDurationSeconds = consonant;
        row.alignmentSeconds = offset + preutter;
        // UTAU uses a negative cutoff as the region length from offset, while
        // a positive cutoff trims that amount from the physical file end.
        row.regionEndSeconds = cutoffMs < 0.0
            ? offset + (-cutoffMs) / 1000.0
            : audioDuration > 0.0 ? audioDuration - cutoffMs / 1000.0
                                  : offset + std::max({ 0.05, consonant, preutter });
        if (audioDuration > 0.0)
            row.regionEndSeconds = juce::jlimit(offset + 0.001,
                                                std::max(offset + 0.001, audioDuration),
                                                row.regionEndSeconds);
        else row.regionEndSeconds = std::max(offset + 0.001, row.regionEndSeconds);
        row.alignmentSeconds = juce::jlimit(row.regionStartSeconds, row.regionEndSeconds,
                                            row.alignmentSeconds);
        row.fixedDurationSeconds = juce::jlimit(0.0,
            row.regionEndSeconds - row.regionStartSeconds, row.fixedDurationSeconds);
        rows.push_back(std::move(row));
    }
    if (rows.empty())
    {
        error = audio == juce::File{} ? "oto.ini contains no valid entries"
                                      : "oto.ini contains no entries for " + audio.getFileName();
        return false;
    }
    return true;
}

bool SampleSettings::exportOto(const juce::File& oto, const juce::File& audio,
                               const std::vector<SampleRegionSetting>& rows,
                               double audioDuration, juce::String& error)
{
    juce::String output;
    for (const auto& row : rows)
    {
        const auto cutoff = -std::max(0.0, audioDuration - row.regionEndSeconds) * 1000.0;
        output += audio.getFileName() + "=" + row.name + ","
            + juce::String(row.regionStartSeconds * 1000.0, 3) + ","
            + juce::String(row.fixedDurationSeconds * 1000.0, 3) + ","
            + juce::String(cutoff, 3) + ","
            + juce::String((row.alignmentSeconds - row.regionStartSeconds) * 1000.0, 3)
            + ",0\n";
    }
    if (!oto.replaceWithText(output, false, false, "\n"))
    {
        error = "Could not write " + oto.getFullPathName();
        return false;
    }
    return true;
}

bool SampleSettings::importVoicebank(const juce::File& root, juce::StringArray& audioFiles,
                                     int& sidecarsWritten, int& regionsWritten,
                                     juce::StringArray& warnings)
{
    audioFiles.clear();
    warnings.clear();
    sidecarsWritten = 0;
    regionsWritten = 0;
    juce::Array<juce::File> otoFiles;
    if (root.existsAsFile() && root.getFileName().equalsIgnoreCase("oto.ini"))
        otoFiles.add(root);
    else if (root.isDirectory())
    {
        juce::Array<juce::File> candidates;
        root.findChildFiles(candidates, juce::File::findFiles, true, "*");
        for (const auto& candidate : candidates)
            if (candidate.getFileName().equalsIgnoreCase("oto.ini")) otoFiles.add(candidate);
    }
    if (otoFiles.isEmpty())
    {
        warnings.add("oto.ini not found under " + root.getFullPathName());
        return false;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (const auto& oto : otoFiles)
    {
        juce::Array<juce::File> samples;
        oto.getParentDirectory().findChildFiles(samples, juce::File::findFiles, false, "*");
        for (const auto& sample : samples)
        {
            if (!sample.hasFileExtension("wav;flac;aif;aiff;mp3;ogg")) continue;
            audioFiles.addIfNotAlreadyThere(sample.getFullPathName(), false);
            auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(sample));
            if (reader == nullptr || reader->sampleRate <= 0.0) continue;
            const auto duration = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
            std::vector<SampleRegionSetting> rows;
            juce::String error;
            if (!importOto(oto, sample, duration, rows, error)) continue;
            if (!save(sample, rows, error))
            {
                warnings.add(error);
                continue;
            }
            ++sidecarsWritten;
            regionsWritten += static_cast<int>(rows.size());
        }
    }
    return !audioFiles.isEmpty();
}
}
