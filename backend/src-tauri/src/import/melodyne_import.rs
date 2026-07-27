//! Minimal, bounds-checked Melodyne 5 project importer.
//!
//! Melodyne `.mpd` files are GNBCFA containers whose graph entries are
//! `GNBinaryKeyValueArchive` payloads.  The object graph itself is private and
//! versioned, but audio sources are stored as ordinary `GNFilePath` strings.
//! Importing those paths is enough to create a playable HachiShifter session;
//! unresolved sources are intentionally retained so the existing relink UI can
//! locate them.

use crate::state::{PitchAnalysisAlgo, TimelineState};
use flate2::read::ZlibDecoder;
use std::collections::HashSet;
use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};

const ARCHIVE_MAGIC: &[u8; 6] = b"GNBCFA";
const COMPRESSED_MAGIC: &[u8; 8] = b"GNBKVAi\0";
const MAX_DECODED_ENTRY_BYTES: u64 = 256 * 1024 * 1024;

#[derive(Debug)]
pub struct MelodyneImportResult {
    pub timeline: TimelineState,
    pub missing_files: Vec<String>,
    pub referenced_files: Vec<String>,
}

fn read_u32(data: &[u8], offset: usize) -> Result<u32, String> {
    let bytes: [u8; 4] = data
        .get(offset..offset.saturating_add(4))
        .ok_or_else(|| format!("truncated MPD u32 at 0x{offset:x}"))?
        .try_into()
        .map_err(|_| "invalid MPD u32".to_string())?;
    Ok(u32::from_le_bytes(bytes))
}

fn read_u64(data: &[u8], offset: usize) -> Result<u64, String> {
    let bytes: [u8; 8] = data
        .get(offset..offset.saturating_add(8))
        .ok_or_else(|| format!("truncated MPD u64 at 0x{offset:x}"))?
        .try_into()
        .map_err(|_| "invalid MPD u64".to_string())?;
    Ok(u64::from_le_bytes(bytes))
}

fn align_8(value: usize) -> Result<usize, String> {
    value
        .checked_add(7)
        .map(|value| value & !7)
        .ok_or_else(|| "MPD offset overflow".to_string())
}

fn decode_graph_entries(data: &[u8]) -> Result<Vec<Vec<u8>>, String> {
    if data.len() < 8 || data.get(..6) != Some(ARCHIVE_MAGIC.as_slice()) {
        return Err("not a Melodyne GNBCFA project".to_string());
    }
    let mut offset = 8usize;
    let mut entries = Vec::new();
    while offset < data.len() {
        let name_length = read_u32(data, offset)? as usize;
        offset = offset
            .checked_add(8)
            .ok_or_else(|| "MPD offset overflow".to_string())?;
        let name_end = offset
            .checked_add(name_length)
            .ok_or_else(|| "MPD entry name overflow".to_string())?;
        if name_end > data.len() {
            return Err("truncated MPD entry name".to_string());
        }
        offset = align_8(name_end)?;
        let stored_length = read_u64(data, offset)? as usize;
        offset = offset
            .checked_add(8)
            .ok_or_else(|| "MPD offset overflow".to_string())?;
        let stored_end = offset
            .checked_add(stored_length)
            .ok_or_else(|| "MPD payload length overflow".to_string())?;
        let stored = data
            .get(offset..stored_end)
            .ok_or_else(|| "truncated MPD entry payload".to_string())?;
        offset = stored_end;

        if stored.len() >= 20 && stored.get(..8) == Some(COMPRESSED_MAGIC.as_slice()) {
            let compressed_length = read_u32(stored, 16)? as usize;
            if compressed_length != stored.len() - 20 {
                return Err("invalid compressed MPD entry length".to_string());
            }
            let mut decoder = ZlibDecoder::new(&stored[20..]);
            let mut decoded = Vec::new();
            decoder
                .by_ref()
                .take(MAX_DECODED_ENTRY_BYTES + 1)
                .read_to_end(&mut decoded)
                .map_err(|error| format!("failed to decompress MPD graph: {error}"))?;
            if decoded.len() as u64 > MAX_DECODED_ENTRY_BYTES {
                return Err("MPD graph entry exceeds the decode limit".to_string());
            }
            entries.push(decoded);
        } else {
            entries.push(stored.to_vec());
        }
    }
    Ok(entries)
}

fn has_audio_extension(value: &str) -> bool {
    let lower = value.trim().to_ascii_lowercase();
    [".wav", ".wave", ".aif", ".aiff", ".flac", ".mp3", ".ogg", ".m4a"]
        .iter()
        .any(|extension| lower.ends_with(extension))
}

fn clean_path_candidate(value: &str) -> Option<String> {
    let value = value
        .trim_matches(|ch: char| ch.is_whitespace() || matches!(ch, '\0' | '"' | '\''))
        .trim();
    if value.len() >= 4 && has_audio_extension(value) {
        Some(value.to_string())
    } else {
        None
    }
}

fn extract_ascii_paths(data: &[u8], output: &mut Vec<String>) {
    let mut start = None;
    for index in 0..=data.len() {
        let byte = data.get(index).copied().unwrap_or(0);
        if (0x20..=0x7e).contains(&byte) {
            start.get_or_insert(index);
        } else if let Some(begin) = start.take() {
            if index.saturating_sub(begin) >= 4 {
                if let Ok(value) = std::str::from_utf8(&data[begin..index]) {
                    if let Some(path) = clean_path_candidate(value) {
                        output.push(path);
                    }
                }
            }
        }
    }
}

fn extract_utf16_paths(data: &[u8], output: &mut Vec<String>) {
    // GNFilePath is UTF-16LE. Scan both byte alignments because a future
    // archive entry may place the value at an odd payload offset.
    for alignment in 0..=1usize {
        let mut index = alignment;
        let mut run = Vec::<u16>::new();
        while index + 1 < data.len() {
            let unit = u16::from_le_bytes([data[index], data[index + 1]]);
            let valid = unit != 0
                && char::from_u32(unit as u32)
                    .map(|ch| !ch.is_control())
                    .unwrap_or(false);
            if valid {
                run.push(unit);
            } else {
                if run.len() >= 4 {
                    let value = String::from_utf16_lossy(&run);
                    if let Some(path) = clean_path_candidate(&value) {
                        output.push(path);
                    }
                }
                run.clear();
            }
            index += 2;
        }
        if run.len() >= 4 {
            let value = String::from_utf16_lossy(&run);
            if let Some(path) = clean_path_candidate(&value) {
                output.push(path);
            }
        }
    }
}

pub fn referenced_audio_paths(data: &[u8]) -> Result<Vec<String>, String> {
    let entries = decode_graph_entries(data)?;
    let mut paths = Vec::new();
    for entry in entries {
        extract_ascii_paths(&entry, &mut paths);
        extract_utf16_paths(&entry, &mut paths);
    }
    let mut seen = HashSet::new();
    paths.retain(|path| seen.insert(path.to_lowercase()));
    if paths.is_empty() {
        return Err("Melodyne project contains no referenced audio media".to_string());
    }
    Ok(paths)
}

fn file_name_from_foreign_path(value: &str) -> Option<&str> {
    value
        .rsplit(['/', '\\'])
        .find(|part| !part.trim().is_empty())
}

fn resolve_media_path(stored: &str, project_dir: &Path) -> Option<PathBuf> {
    let stored_path = PathBuf::from(stored);
    if stored_path.is_file() {
        return fs::canonicalize(&stored_path).ok().or(Some(stored_path));
    }
    let normalized = stored.replace('\\', "/");
    let relative = project_dir.join(&normalized);
    if relative.is_file() {
        return fs::canonicalize(&relative).ok().or(Some(relative));
    }
    let file_name = file_name_from_foreign_path(stored)?;
    let beside = project_dir.join(file_name);
    if beside.is_file() {
        return fs::canonicalize(&beside).ok().or(Some(beside));
    }

    // Melodyne users often keep an `Audio Files` folder beside the project.
    // Search only one directory level to keep project opening instant.
    let wanted = file_name.to_lowercase();
    let entries = fs::read_dir(project_dir).ok()?;
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_file()
            && path
                .file_name()
                .and_then(|name| name.to_str())
                .map(|name| name.to_lowercase() == wanted)
                .unwrap_or(false)
        {
            return fs::canonicalize(&path).ok().or(Some(path));
        }
        if !path.is_dir() {
            continue;
        }
        for child in fs::read_dir(&path).ok()?.flatten() {
            let child_path = child.path();
            if child_path.is_file()
                && child_path
                    .file_name()
                    .and_then(|name| name.to_str())
                    .map(|name| name.to_lowercase() == wanted)
                    .unwrap_or(false)
            {
                return fs::canonicalize(&child_path).ok().or(Some(child_path));
            }
        }
    }
    None
}

pub fn import_mpd(path: &Path, data: &[u8]) -> Result<MelodyneImportResult, String> {
    let referenced_files = referenced_audio_paths(data)?;
    let project_dir = path.parent().unwrap_or_else(|| Path::new("."));
    let mut timeline = TimelineState::default();
    timeline.clips.clear();
    let mut missing_files = Vec::new();

    for (index, stored) in referenced_files.iter().enumerate() {
        let resolved = resolve_media_path(stored, project_dir);
        let import_path = resolved
            .as_ref()
            .map(|value| value.to_string_lossy().to_string())
            .unwrap_or_else(|| stored.clone());
        if resolved.is_none() {
            missing_files.push(stored.clone());
        }

        let media_name = file_name_from_foreign_path(stored)
            .unwrap_or("Melodyne Audio")
            .to_string();
        let track_id = if index == 0 {
            let track = &mut timeline.tracks[0];
            track.name = media_name.clone();
            track.id.clone()
        } else {
            timeline.add_track(Some(media_name), None, None)
        };
        if let Some(track) = timeline.tracks.iter_mut().find(|track| track.id == track_id) {
            // Playback remains immediate (raw media until the user enables
            // composition), while the imported track's selected pitch engine
            // is already the model-free mld5 path.
            track.compose_enabled = false;
            track.pitch_analysis_algo = PitchAnalysisAlgo::Mld5;
        }
        timeline.import_audio_item(&import_path, Some(track_id), Some(0.0));
    }

    timeline.playhead_sec = 0.0;
    timeline.project_sec = timeline
        .clips
        .iter()
        .map(|clip| clip.start_sec + clip.length_sec)
        .fold(4.0f64, f64::max)
        .ceil();
    timeline.selected_track_id = timeline.tracks.first().map(|track| track.id.clone());
    timeline.selected_clip_id = timeline.clips.first().map(|clip| clip.id.clone());

    Ok(MelodyneImportResult {
        timeline,
        missing_files,
        referenced_files,
    })
}

#[cfg(test)]
mod tests {
    use super::{extract_utf16_paths, has_audio_extension};

    #[test]
    fn extracts_gnfilepath_utf16_audio() {
        let mut data = vec![3, 4, 5, 6];
        data.extend("C:/Audio/vocal.wav".encode_utf16().flat_map(u16::to_le_bytes));
        data.extend([0, 0]);
        let mut paths = Vec::new();
        extract_utf16_paths(&data, &mut paths);
        assert!(paths.iter().any(|path| path == "C:/Audio/vocal.wav"));
    }

    #[test]
    fn recognizes_supported_media_extensions() {
        assert!(has_audio_extension("take.AIFF"));
        assert!(!has_audio_extension("project.mpd"));
    }
}
