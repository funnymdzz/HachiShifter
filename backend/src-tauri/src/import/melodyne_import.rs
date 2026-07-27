//! Bounds-checked Melodyne 5 (`.mpd`) project importer.
//!
//! Melodyne stores its session as a GNBCFA container.  Recent projects contain
//! a `GNBinaryKeyValueArchive` object graph describing the track tree, source
//! media, note elements, time maps and the edits applied to every note.  This
//! importer reads that graph directly so a session is not flattened into one
//! HachiShifter track per file.

use crate::state::{PitchAnalysisAlgo, TimelineState, TrackParamsState};
use flate2::read::ZlibDecoder;
use std::collections::{BTreeMap, HashMap, HashSet};
use std::fs;
use std::io::Read;
use std::ops::Range;
use std::path::{Path, PathBuf};

const ARCHIVE_MAGIC: &[u8; 6] = b"GNBCFA";
const COMPRESSED_MAGIC: &[u8; 8] = b"GNBKVAi\0";
const MAX_DECODED_ENTRY_BYTES: u64 = 256 * 1024 * 1024;
const MAX_KEYS: usize = 16_384;
const MAX_CLASSES: usize = 2_048;
const MAX_OBJECTS: usize = 2_000_000;
const IMPORT_FRAME_PERIOD_MS: f64 = 5.0;

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

#[derive(Clone, Debug)]
struct FieldSchema {
    name: String,
    value_type: u32,
}

#[derive(Clone, Debug)]
struct ClassSchema {
    name: String,
    fields: Vec<FieldSchema>,
}

#[derive(Clone, Copy, Debug)]
enum Value {
    Ref(Option<u32>),
    Bool(bool),
    I32(i32),
    I64(i64),
    F32(f32),
    F64(f64),
}

struct Graph {
    data: Vec<u8>,
    classes: Vec<ClassSchema>,
    object_classes: Vec<usize>,
    records: Vec<Option<Range<usize>>>,
}

fn value_size(value_type: u32) -> Option<usize> {
    match value_type {
        0x85e => Some(8),  // object reference
        0x162 | 0x163 => Some(1),
        0x469 | 0x466 => Some(4),
        0x864 | 0x86c | 0x871 => Some(8),
        0x1052 => Some(16),
        _ => None,
    }
}

fn checked_take<'a>(data: &'a [u8], offset: &mut usize, len: usize) -> Result<&'a [u8], String> {
    let end = offset
        .checked_add(len)
        .ok_or_else(|| "MPD graph offset overflow".to_string())?;
    let value = data
        .get(*offset..end)
        .ok_or_else(|| "truncated MPD graph".to_string())?;
    *offset = end;
    Ok(value)
}

fn take_u32(data: &[u8], offset: &mut usize) -> Result<u32, String> {
    let bytes: [u8; 4] = checked_take(data, offset, 4)?
        .try_into()
        .map_err(|_| "invalid MPD graph u32".to_string())?;
    Ok(u32::from_le_bytes(bytes))
}

impl Graph {
    fn parse(data: Vec<u8>) -> Result<Self, String> {
        let mut offset = 0usize;
        let key_count = take_u32(&data, &mut offset)? as usize;
        if key_count == 0 || key_count > MAX_KEYS {
            return Err("invalid MPD graph key count".to_string());
        }
        let mut keys = Vec::with_capacity(key_count);
        for _ in 0..key_count {
            let len = take_u32(&data, &mut offset)? as usize;
            let raw = checked_take(&data, &mut offset, len)?;
            keys.push(
                std::str::from_utf8(raw)
                    .map_err(|_| "invalid MPD graph key".to_string())?
                    .to_string(),
            );
        }

        let class_count = take_u32(&data, &mut offset)? as usize;
        if class_count == 0 || class_count > MAX_CLASSES {
            return Err("invalid MPD graph class count".to_string());
        }
        let mut classes = Vec::with_capacity(class_count);
        for _ in 0..class_count {
            let name_len = take_u32(&data, &mut offset)? as usize;
            let name = std::str::from_utf8(checked_take(&data, &mut offset, name_len)?)
                .map_err(|_| "invalid MPD graph class name".to_string())?
                .to_string();
            let _base_or_version = take_u32(&data, &mut offset)?;
            let field_count = take_u32(&data, &mut offset)? as usize;
            if field_count > MAX_KEYS {
                return Err("invalid MPD class field count".to_string());
            }
            let mut fields = Vec::with_capacity(field_count);
            for _ in 0..field_count {
                let key_index = take_u32(&data, &mut offset)? as usize;
                let _multiplicity = take_u32(&data, &mut offset)?;
                let value_type = take_u32(&data, &mut offset)?;
                let _flags = checked_take(&data, &mut offset, 1)?[0];
                let field_name = keys
                    .get(key_index)
                    .ok_or_else(|| "invalid MPD field key index".to_string())?
                    .clone();
                if value_size(value_type).is_none() {
                    return Err(format!("unsupported MPD value type 0x{value_type:x}"));
                }
                fields.push(FieldSchema {
                    name: field_name,
                    value_type,
                });
            }
            classes.push(ClassSchema { name, fields });
        }

        let object_count = take_u32(&data, &mut offset)? as usize;
        if object_count == 0 || object_count > MAX_OBJECTS {
            return Err("invalid MPD object count".to_string());
        }
        let mut object_classes = Vec::with_capacity(object_count);
        for _ in 0..object_count {
            let class_id = take_u32(&data, &mut offset)? as usize;
            if class_id >= classes.len() {
                return Err("invalid MPD object class".to_string());
            }
            object_classes.push(class_id);
        }

        let serialized_count = take_u32(&data, &mut offset)? as usize;
        if serialized_count > object_count {
            return Err("invalid MPD serialized object count".to_string());
        }
        let mut object_ids = Vec::with_capacity(serialized_count);
        for _ in 0..serialized_count {
            let object_id = take_u32(&data, &mut offset)? as usize;
            if object_id >= object_count {
                return Err("invalid MPD serialized object id".to_string());
            }
            object_ids.push(object_id);
        }

        let mut records = vec![None; object_count];
        for object_id in object_ids {
            let start = offset;
            let payload_len = take_u32(&data, &mut offset)? as usize;
            let end = offset
                .checked_add(payload_len)
                .ok_or_else(|| "MPD object length overflow".to_string())?;
            if end > data.len() {
                return Err("truncated MPD object record".to_string());
            }
            records[object_id] = Some(start..end);
            offset = end;
        }
        Ok(Self {
            data,
            classes,
            object_classes,
            records,
        })
    }

    fn record(&self, object_id: u32) -> Option<&[u8]> {
        let range = self.records.get(object_id as usize)?.as_ref()?;
        self.data.get(range.clone())
    }

    fn class(&self, object_id: u32) -> Option<&ClassSchema> {
        let class_id = *self.object_classes.get(object_id as usize)?;
        self.classes.get(class_id)
    }

    fn class_name(&self, object_id: u32) -> Option<&str> {
        Some(self.class(object_id)?.name.as_str())
    }

    fn value(&self, object_id: u32, wanted: &str) -> Option<Value> {
        let record = self.record(object_id)?;
        let class = self.class(object_id)?;
        let mut offset = 8usize;
        for field in &class.fields {
            let size = value_size(field.value_type)?;
            let bytes = record.get(offset..offset.checked_add(size)?)?;
            if field.name == wanted {
                return match field.value_type {
                    0x85e => {
                        let raw = u32::from_le_bytes(bytes.get(..4)?.try_into().ok()?);
                        Some(Value::Ref((raw != u32::MAX).then_some(raw)))
                    }
                    0x162 => Some(Value::Bool(bytes[0] != 0)),
                    0x469 => Some(Value::I32(i32::from_le_bytes(bytes.try_into().ok()?))),
                    0x466 => Some(Value::F32(f32::from_le_bytes(bytes.try_into().ok()?))),
                    0x864 => Some(Value::F64(f64::from_le_bytes(bytes.try_into().ok()?))),
                    0x86c => Some(Value::I64(i64::from_le_bytes(bytes.try_into().ok()?))),
                    _ => None,
                };
            }
            offset += size;
        }
        None
    }

    fn reference(&self, object_id: u32, field: &str) -> Option<u32> {
        match self.value(object_id, field)? {
            Value::Ref(value) => value,
            _ => None,
        }
    }

    fn bool(&self, object_id: u32, field: &str) -> bool {
        matches!(self.value(object_id, field), Some(Value::Bool(true)))
    }

    fn f32(&self, object_id: u32, field: &str) -> Option<f32> {
        match self.value(object_id, field)? {
            Value::F32(value) => value.is_finite().then_some(value),
            _ => None,
        }
    }

    fn f64(&self, object_id: u32, field: &str) -> Option<f64> {
        match self.value(object_id, field)? {
            Value::F64(value) => value.is_finite().then_some(value),
            _ => None,
        }
    }

    fn i32(&self, object_id: u32, field: &str) -> Option<i32> {
        match self.value(object_id, field)? {
            Value::I32(value) => Some(value),
            _ => None,
        }
    }

    fn i64(&self, object_id: u32, field: &str) -> Option<i64> {
        match self.value(object_id, field)? {
            Value::I64(value) => Some(value),
            _ => None,
        }
    }

    fn list(&self, object_id: u32) -> Vec<u32> {
        if self.class_name(object_id) != Some("GNList") {
            return Vec::new();
        }
        let Some(record) = self.record(object_id) else {
            return Vec::new();
        };
        let Ok(count) = read_u32(record, 29) else {
            return Vec::new();
        };
        let count = count as usize;
        if count > MAX_OBJECTS || 33usize.saturating_add(count.saturating_mul(4)) > record.len() {
            return Vec::new();
        }
        (0..count)
            .filter_map(|index| read_u32(record, 33 + index * 4).ok())
            .collect()
    }

    fn string(&self, object_id: u32) -> Option<String> {
        if self.class_name(object_id) != Some("GNString") {
            return None;
        }
        let record = self.record(object_id)?;
        let fixed_len = read_u32(record, 4).ok()? as usize;
        let extra = 8usize.checked_add(fixed_len)?;
        let byte_len = read_u32(record, extra.checked_add(12)?).ok()? as usize;
        let start = extra.checked_add(20)?;
        let raw = record.get(start..start.checked_add(byte_len)?)?;
        let encoding = self.i32(object_id, "encoding").unwrap_or(1);
        let value = if encoding == 5 {
            let units: Vec<u16> = raw
                .chunks_exact(2)
                .map(|pair| u16::from_le_bytes([pair[0], pair[1]]))
                .take_while(|unit| *unit != 0)
                .collect();
            String::from_utf16(&units).ok()?
        } else {
            std::str::from_utf8(raw.strip_suffix(&[0]).unwrap_or(raw))
                .ok()?
                .to_string()
        };
        (!value.trim().is_empty()).then(|| value.trim().to_string())
    }

    fn string_field(&self, object_id: u32, field: &str) -> Option<String> {
        self.string(self.reference(object_id, field)?)
    }

    fn find_first_class(&self, class_name: &str) -> Option<u32> {
        self.object_classes
            .iter()
            .enumerate()
            .find(|(object_id, _)| self.class_name(*object_id as u32) == Some(class_name))
            .map(|(object_id, _)| object_id as u32)
    }

    fn function_points(&self, function_id: u32) -> Vec<(f64, f64)> {
        match self.class_name(function_id) {
            Some("MUConstantFunction") => {
                vec![(0.0, self.f64(function_id, "y").unwrap_or(0.0))]
            }
            Some("MUDataPointLinearFunction") | Some("MUBezierDataPointFunction") => {
                let Some(points_id) = self.reference(function_id, "points") else {
                    return Vec::new();
                };
                let mut points: Vec<(f64, f64)> = self
                    .list(points_id)
                    .into_iter()
                    .filter_map(|point| Some((self.f64(point, "x")?, self.f64(point, "y")?)))
                    .collect();
                points.sort_by(|left, right| left.0.total_cmp(&right.0));
                points
            }
            _ => Vec::new(),
        }
    }

    fn eval_function(&self, function_id: u32, x: f64) -> f64 {
        let points = self.function_points(function_id);
        if points.is_empty() {
            return x;
        }
        if points.len() == 1 {
            return points[0].1;
        }
        if x <= points[0].0 {
            return points[0].1;
        }
        for pair in points.windows(2) {
            if x <= pair[1].0 {
                let span = (pair[1].0 - pair[0].0).max(1e-9);
                let fraction = ((x - pair[0].0) / span).clamp(0.0, 1.0);
                // A linear interpolation is deliberate here.  It keeps the
                // exact endpoints of Melodyne's Bezier warp and gives stable
                // transport in HachiShifter's uniform-rate clip engine.
                return pair[0].1 + (pair[1].1 - pair[0].1) * fraction;
            }
        }
        points.last().map(|point| point.1).unwrap_or(x)
    }
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

#[derive(Clone, Debug)]
struct SourceInfo {
    stored_path: String,
    sample_rate: f64,
}

#[derive(Clone, Debug)]
struct ImportedElement {
    object_id: u32,
    start: f64,
    duration: f64,
    source_id: u32,
    source_item_id: u32,
    source_description_id: u32,
    source_function_id: Option<u32>,
    pitch_center: f32,
    pitch_drift: f32,
    pitch_modulation: f32,
    formant_offset: f32,
    amplitude_factor: f32,
    muted: bool,
    join_next: bool,
    join_duration: f64,
}

#[derive(Clone, Debug)]
struct ImportedTrack {
    name: String,
    muted: bool,
    solo: bool,
    melodic: bool,
    elements: Vec<ImportedElement>,
}

fn element_source_chain(graph: &Graph, element_id: u32) -> Option<(u32, u32, u32)> {
    let audio_components = graph.list(graph.reference(element_id, "audioComponents")?);
    let component = graph
        .reference(element_id, "principalAudioComponent")
        .or_else(|| audio_components.first().copied())?;
    let source_component = graph.reference(component, "audioSourceComponent")?;
    let source_item = graph.reference(source_component, "audioSourceItem")?;
    let source_element = graph.reference(source_component, "audioSourceElement")?;
    let source_description = graph.reference(source_element, "audioSourceDescription")?;
    let source_id = graph.reference(source_description, "audioSource")?;
    Some((source_id, source_item, source_description))
}

fn is_melodic_description(graph: &Graph, description_id: u32) -> bool {
    let Some(parameter_set) = graph.reference(description_id, "analyzerParameterSet") else {
        return false;
    };
    let identifier = graph
        .string_field(parameter_set, "identifier")
        .unwrap_or_default()
        .to_ascii_lowercase();
    identifier.contains(".melodic") || graph.bool(parameter_set, "isTonalicOnly")
}

fn collect_track_ids(graph: &Graph, track_id: u32, output: &mut Vec<u32>, seen: &mut HashSet<u32>) {
    if !seen.insert(track_id) {
        return;
    }
    let elements = graph
        .reference(track_id, "elements")
        .map(|list| graph.list(list))
        .unwrap_or_default();
    if !elements.is_empty() {
        output.push(track_id);
    }
    if let Some(subtracks) = graph.reference(track_id, "subtracks") {
        for child in graph.list(subtracks) {
            collect_track_ids(graph, child, output, seen);
        }
    }
}

fn collect_project(graph: &Graph) -> Result<(Vec<ImportedTrack>, BTreeMap<u32, SourceInfo>), String> {
    let performance = graph
        .find_first_class("MUPerformance")
        .ok_or_else(|| "Melodyne graph has no performance".to_string())?;
    let root_track = graph
        .reference(performance, "rootTrack")
        .ok_or_else(|| "Melodyne performance has no root track".to_string())?;

    let mut sources = BTreeMap::new();
    if let Some(source_list) = graph.reference(performance, "audioSources") {
        for source_id in graph.list(source_list) {
            if graph.class_name(source_id) != Some("MUAudioFileSource") {
                continue;
            }
            let Some(file_path) = graph.reference(source_id, "filePath") else {
                continue;
            };
            let Some(stored_path) = graph.string_field(file_path, "posixPath") else {
                continue;
            };
            sources.insert(
                source_id,
                SourceInfo {
                    stored_path,
                    sample_rate: graph.f64(source_id, "sampleRate").unwrap_or(44_100.0).max(1.0),
                },
            );
        }
    }

    let mut track_ids = Vec::new();
    collect_track_ids(graph, root_track, &mut track_ids, &mut HashSet::new());
    let mut tracks = Vec::new();
    for (track_index, track_id) in track_ids.into_iter().enumerate() {
        let name = graph
            .string_field(track_id, "title")
            .unwrap_or_else(|| format!("Melodyne Track {}", track_index + 1));
        let declared_mode = graph
            .string_field(track_id, "defaultAnalyzerParameterSetIdenfier")
            .unwrap_or_default()
            .to_ascii_lowercase();
        let mut melodic = declared_mode.contains(".melodic");
        let mut elements = Vec::new();
        if let Some(element_list) = graph.reference(track_id, "elements") {
            for element_id in graph.list(element_list) {
                if graph.class_name(element_id) != Some("MUElement") {
                    continue;
                }
                let Some((source_id, source_item_id, source_description_id)) =
                    element_source_chain(graph, element_id)
                else {
                    continue;
                };
                if !sources.contains_key(&source_id) {
                    continue;
                }
                melodic |= is_melodic_description(graph, source_description_id);
                let start = graph.f64(element_id, "startTime").unwrap_or(0.0);
                let duration = graph.f64(element_id, "duration").unwrap_or(0.0);
                if !start.is_finite() || !duration.is_finite() || duration <= 0.0 {
                    continue;
                }
                let following_join = graph.reference(element_id, "followingJoin");
                elements.push(ImportedElement {
                    object_id: element_id,
                    start,
                    duration,
                    source_id,
                    source_item_id,
                    source_description_id,
                    source_function_id: graph.reference(element_id, "sourceTimeForElementTimeFunction"),
                    pitch_center: graph.f32(element_id, "pitchCenter").unwrap_or(0.0),
                    pitch_drift: graph.f32(element_id, "pitchDriftFactor").unwrap_or(1.0),
                    pitch_modulation: graph
                        .f32(element_id, "pitchModulationFactor")
                        .unwrap_or(1.0),
                    formant_offset: graph.f32(element_id, "formantOffset").unwrap_or(0.0),
                    amplitude_factor: graph.f32(element_id, "amplitudeFactor").unwrap_or(1.0),
                    muted: graph.bool(element_id, "isMuted"),
                    join_next: following_join
                        .map(|join| graph.bool(join, "joinsPitches"))
                        .unwrap_or(false),
                    join_duration: following_join
                        .and_then(|join| graph.f64(join, "pitchTransitionDuration"))
                        .unwrap_or(0.0),
                });
            }
        }
        elements.sort_by(|left, right| left.start.total_cmp(&right.start));
        if !elements.is_empty() {
            tracks.push(ImportedTrack {
                name,
                muted: graph.bool(track_id, "isMuted"),
                solo: graph.bool(track_id, "isSolo"),
                melodic,
                elements,
            });
        }
    }
    if tracks.is_empty() {
        return Err("Melodyne project contains no playable track elements".to_string());
    }
    Ok((tracks, sources))
}

fn source_time(graph: &Graph, element: &ImportedElement, local_time: f64) -> f64 {
    let item_start_samples = graph
        .i64(element.source_item_id, "startSampleIndex")
        .unwrap_or(0)
        .max(0) as f64;
    let sample_rate = graph
        .reference(element.source_description_id, "audioSource")
        .and_then(|source| graph.f64(source, "sampleRate"))
        .unwrap_or(44_100.0)
        .max(1.0);
    let mapped = element
        .source_function_id
        .map(|function| graph.eval_function(function, local_time.clamp(0.0, element.duration)))
        .unwrap_or(local_time);
    item_start_samples / sample_rate + mapped.max(0.0)
}

#[derive(Clone, Debug)]
struct ClipGroup {
    source_id: u32,
    placement: f64,
    element_indices: Vec<usize>,
    timeline_start: f64,
    timeline_end: f64,
    source_start: f64,
    source_end: f64,
}

fn group_clips(graph: &Graph, track: &ImportedTrack) -> Vec<ClipGroup> {
    let mut groups: Vec<ClipGroup> = Vec::new();
    for (index, element) in track.elements.iter().enumerate() {
        let source_start = source_time(graph, element, 0.0);
        let source_end = source_time(graph, element, element.duration).max(source_start + 1e-6);
        let placement = element.start - source_start;
        // Elements from one source occurrence share a placement.  A 50 ms
        // tolerance retains manually warped notes while keeping a later reuse
        // of the same sample as a distinct clip.
        if let Some(group) = groups.iter_mut().find(|group| {
            group.source_id == element.source_id && (group.placement - placement).abs() <= 0.05
        }) {
            group.element_indices.push(index);
            group.timeline_start = group.timeline_start.min(element.start);
            group.timeline_end = group.timeline_end.max(element.start + element.duration);
            group.source_start = group.source_start.min(source_start);
            group.source_end = group.source_end.max(source_end);
            let count = group.element_indices.len() as f64;
            group.placement += (placement - group.placement) / count;
        } else {
            groups.push(ClipGroup {
                source_id: element.source_id,
                placement,
                element_indices: vec![index],
                timeline_start: element.start,
                timeline_end: element.start + element.duration,
                source_start,
                source_end,
            });
        }
    }
    groups.sort_by(|left, right| left.timeline_start.total_cmp(&right.timeline_start));
    groups
}

fn source_pitch_at(graph: &Graph, element: &ImportedElement, local_time: f64) -> Option<(f32, f32)> {
    let property_points = graph.reference(element.source_item_id, "propertyPoints")?;
    let points = graph.list(property_points);
    if points.is_empty() {
        return None;
    }
    let values_per_second = graph
        .f64(element.source_description_id, "parameterValuesPerSecond")
        .unwrap_or(200.0)
        .max(1.0);
    let source_local = element
        .source_function_id
        .map(|function| graph.eval_function(function, local_time))
        .unwrap_or(local_time)
        .max(0.0);
    let point_position = source_local * values_per_second;
    let left_index = point_position.floor().max(0.0) as usize;
    let right_index = (left_index + 1).min(points.len() - 1);
    let fraction = (point_position - left_index as f64).clamp(0.0, 1.0) as f32;
    let read = |point_id: u32, name: &str| graph.f32(point_id, name);
    let left = *points.get(left_index.min(points.len() - 1))?;
    let right = points[right_index];
    if graph.bool(left, "isConsideredSilent") && graph.bool(right, "isConsideredSilent") {
        return None;
    }
    let interpolate = |name: &str| {
        let a = read(left, name)?;
        let b = read(right, name).unwrap_or(a);
        Some(a + (b - a) * fraction)
    };
    Some((interpolate("pitchCent")?, interpolate("pitchWithoutVibrato")?))
}

fn smooth_curve(curve: &mut [f32], center: usize, radius: usize) {
    if curve.is_empty() || center == 0 || center >= curve.len() || radius == 0 {
        return;
    }
    let left = center.saturating_sub(radius);
    let right = center.saturating_add(radius).min(curve.len() - 1);
    let a = curve[left];
    let b = curve[right];
    if a <= 0.0 || b <= 0.0 || right <= left {
        return;
    }
    for (offset, value) in curve[left..=right].iter_mut().enumerate() {
        let phase = offset as f32 / (right - left) as f32;
        let eased = phase * phase * (3.0 - 2.0 * phase);
        *value = a + (b - a) * eased;
    }
}

fn build_track_params(graph: &Graph, track: &ImportedTrack, shift: f64, project_sec: f64) -> TrackParamsState {
    let frame_period_sec = IMPORT_FRAME_PERIOD_MS / 1000.0;
    let frame_count = (project_sec / frame_period_sec).ceil().max(1.0) as usize + 1;
    let mut pitch_orig = vec![0.0f32; frame_count];
    let mut pitch_edit = vec![0.0f32; frame_count];
    let mut formant = vec![0.0f32; frame_count];
    let mut volume = vec![1.0f32; frame_count];

    for element in &track.elements {
        if element.muted {
            continue;
        }
        let start = element.start + shift;
        let first = (start / frame_period_sec).floor().max(0.0) as usize;
        let last = ((start + element.duration) / frame_period_sec)
            .ceil()
            .max(0.0) as usize;
        let source_center = graph
            .f32(element.source_item_id, "pitchCenter")
            .unwrap_or(element.pitch_center);
        for frame in first..last.min(frame_count) {
            let local_time = frame as f64 * frame_period_sec - start;
            let (raw, without_vibrato) = source_pitch_at(graph, element, local_time)
                .unwrap_or((source_center, source_center));
            let edited = element.pitch_center
                + element.pitch_drift * (without_vibrato - source_center)
                + element.pitch_modulation * (raw - without_vibrato);
            pitch_orig[frame] = (raw / 100.0).clamp(0.0, 127.0);
            pitch_edit[frame] = (edited / 100.0).clamp(0.0, 127.0);
            formant[frame] = element.formant_offset;
            volume[frame] = element.amplitude_factor.max(0.0);
        }
    }

    // Preserve Melodyne joins.  Additionally bridge touching samples on one
    // track so their contours do not click or visually break at the edit.
    for pair in track.elements.windows(2) {
        let gap = pair[1].start - (pair[0].start + pair[0].duration);
        if pair[0].join_next || gap.abs() <= 0.05 {
            let boundary = ((pair[1].start + shift) / frame_period_sec).round().max(0.0) as usize;
            let transition = if pair[0].join_duration > 0.0 {
                pair[0].join_duration
            } else {
                0.06
            };
            let radius = ((transition * 0.5) / frame_period_sec).round().max(1.0) as usize;
            smooth_curve(&mut pitch_edit, boundary, radius);
        }
    }

    let mut extra_curves = HashMap::new();
    extra_curves.insert("formant_shift_cents".to_string(), formant);
    extra_curves.insert("volume".to_string(), volume);
    TrackParamsState {
        frame_period_ms: IMPORT_FRAME_PERIOD_MS,
        pitch_orig,
        pitch_edit,
        pitch_edit_user_modified: true,
        extra_curves,
        ..TrackParamsState::default()
    }
}

fn parse_project_graph(data: &[u8]) -> Result<Graph, String> {
    let mut last_error = "Melodyne project contains no object graph".to_string();
    for entry in decode_graph_entries(data)? {
        match Graph::parse(entry) {
            Ok(graph) if graph.find_first_class("MUPerformance").is_some() => return Ok(graph),
            Ok(_) => {}
            Err(error) => last_error = error,
        }
    }
    Err(last_error)
}

fn import_flat_paths(path: &Path, data: &[u8]) -> Result<MelodyneImportResult, String> {
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
            timeline.tracks[0].name = media_name;
            timeline.tracks[0].id.clone()
        } else {
            timeline.add_track(Some(media_name), None, None)
        };
        timeline.import_audio_item(&import_path, Some(track_id), Some(0.0));
    }
    timeline.project_sec = timeline
        .clips
        .iter()
        .map(|clip| clip.start_sec + clip.length_sec)
        .fold(4.0f64, f64::max)
        .ceil();
    Ok(MelodyneImportResult {
        timeline,
        missing_files,
        referenced_files,
    })
}

pub fn import_mpd(path: &Path, data: &[u8]) -> Result<MelodyneImportResult, String> {
    let graph = match parse_project_graph(data) {
        Ok(graph) => graph,
        Err(_) => return import_flat_paths(path, data),
    };
    let (tracks, sources) = collect_project(&graph)?;
    let referenced_files: Vec<String> = sources
        .values()
        .map(|source| source.stored_path.clone())
        .collect();
    let project_dir = path.parent().unwrap_or_else(|| Path::new("."));
    let resolved_sources: BTreeMap<u32, Option<PathBuf>> = sources
        .iter()
        .map(|(source_id, source)| {
            (
                *source_id,
                resolve_media_path(&source.stored_path, project_dir),
            )
        })
        .collect();
    let missing_files: Vec<String> = sources
        .iter()
        .filter(|(source_id, _)| {
            resolved_sources
                .get(source_id)
                .and_then(|path| path.as_ref())
                .is_none()
        })
        .map(|(_, source)| source.stored_path.clone())
        .collect();

    let grouped: Vec<Vec<ClipGroup>> = tracks
        .iter()
        .map(|track| group_clips(&graph, track))
        .collect();
    let earliest = grouped
        .iter()
        .flatten()
        .map(|group| group.timeline_start)
        .fold(0.0f64, f64::min);
    let shift = if earliest < 0.0 { -earliest } else { 0.0 };
    let latest = grouped
        .iter()
        .flatten()
        .map(|group| group.timeline_end + shift)
        .fold(4.0f64, f64::max);
    let project_sec = latest.ceil().max(4.0);

    let mut timeline = TimelineState::default();
    timeline.clips.clear();
    timeline.params_by_root_track.clear();
    timeline.tracks.clear();
    timeline.next_track_order = 0;

    for (track_index, (track, clip_groups)) in tracks.iter().zip(grouped.iter()).enumerate() {
        let track_id = timeline.add_track(Some(track.name.clone()), None, None);
        if let Some(state_track) = timeline.tracks.iter_mut().find(|item| item.id == track_id) {
            state_track.muted = track.muted;
            state_track.solo = track.solo;
            state_track.compose_enabled = track.melodic;
            state_track.pitch_analysis_algo = PitchAnalysisAlgo::Mld5;
        }
        for group in clip_groups {
            let Some(source) = sources.get(&group.source_id) else {
                continue;
            };
            let resolved = resolved_sources
                .get(&group.source_id)
                .and_then(|path| path.as_ref());
            let import_path = resolved
                .map(|path| path.to_string_lossy().to_string())
                .unwrap_or_else(|| source.stored_path.clone());
            let start = group.timeline_start + shift;
            timeline.import_audio_item(&import_path, Some(track_id.clone()), Some(start));
            if let Some(clip) = timeline.clips.last_mut() {
                let timeline_length = (group.timeline_end - group.timeline_start).max(0.001);
                let source_length = (group.source_end - group.source_start).max(0.001);
                clip.name = file_name_from_foreign_path(&source.stored_path)
                    .unwrap_or("Melodyne Audio")
                    .to_string();
                clip.start_sec = start;
                clip.length_sec = timeline_length;
                clip.source_start_sec = group.source_start.max(0.0);
                clip.source_end_sec = group.source_end.max(clip.source_start_sec + 0.001);
                clip.playback_rate = (source_length / timeline_length).clamp(0.05, 20.0) as f32;
                clip.fade_in_sec = clip.fade_in_sec.max(0.005).min(timeline_length * 0.25);
                clip.fade_out_sec = clip.fade_out_sec.max(0.005).min(timeline_length * 0.25);
            }
        }
        if track.melodic {
            timeline.params_by_root_track.insert(
                track_id.clone(),
                build_track_params(&graph, track, shift, project_sec),
            );
        }
        if track_index == 0 {
            timeline.selected_track_id = Some(track_id);
        }
    }

    timeline.playhead_sec = 0.0;
    timeline.project_sec = project_sec;
    timeline.selected_clip_id = timeline.clips.first().map(|clip| clip.id.clone());
    if let Some(first_clip_track) = timeline.clips.first().map(|clip| clip.track_id.clone()) {
        timeline.selected_track_id = Some(first_clip_track);
    }

    Ok(MelodyneImportResult {
        timeline,
        missing_files,
        referenced_files,
    })
}

#[cfg(test)]
mod tests {
    use super::{extract_utf16_paths, has_audio_extension, smooth_curve};

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

    #[test]
    fn connection_smoothing_keeps_endpoints() {
        let mut curve = vec![60.0, 60.0, 60.0, 72.0, 72.0, 72.0];
        smooth_curve(&mut curve, 3, 2);
        assert_eq!(curve[1], 60.0);
        assert_eq!(curve[5], 72.0);
        assert!(curve[3] > 60.0 && curve[3] < 72.0);
    }
}
