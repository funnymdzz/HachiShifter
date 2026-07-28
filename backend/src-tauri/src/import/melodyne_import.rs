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
use std::fs::{self, File};
use std::io::{BufReader, Read, Seek, SeekFrom};
use std::ops::Range;
use std::path::{Path, PathBuf};

const ARCHIVE_MAGIC: &[u8; 6] = b"GNBCFA";
const COMPRESSED_MAGIC: &[u8; 8] = b"GNBKVAi\0";
const MAX_DECODED_ENTRY_BYTES: u64 = 256 * 1024 * 1024;
const MAX_KEYS: usize = 16_384;
const MAX_CLASSES: usize = 2_048;
const MAX_OBJECTS: usize = 2_000_000;
const IMPORT_FRAME_PERIOD_MS: f64 = 5.0;
// Dense edit curves are the largest persistent allocation after an import.
// Keep their aggregate payload small enough that the state and audio-engine
// snapshots can briefly coexist without exhausting typical 8 GB machines.
const MAX_IMPORT_PARAM_BYTES: f64 = 64.0 * 1024.0 * 1024.0;

pub type ImportProgress<'a> = dyn Fn(f64, &str, usize, usize) + 'a;

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

/// Locate and inflate only the object-graph entry.  Unlike `fs::read` plus
/// `decode_graph_entries`, this keeps the compressed MPD container out of
/// memory and never copies unrelated archive entries.
fn decode_graph_file(path: &Path, progress: &ImportProgress<'_>) -> Result<Vec<u8>, String> {
    let file = File::open(path).map_err(|error| format!("failed to open MPD: {error}"))?;
    let file_len = file
        .metadata()
        .map_err(|error| format!("failed to inspect MPD: {error}"))?
        .len()
        .max(1);
    let mut reader = BufReader::with_capacity(64 * 1024, file);
    let mut header = [0u8; 8];
    reader
        .read_exact(&mut header)
        .map_err(|error| format!("failed to read MPD header: {error}"))?;
    if header.get(..6) != Some(ARCHIVE_MAGIC.as_slice()) {
        return Err("not a Melodyne GNBCFA project".to_string());
    }

    loop {
        let position = reader
            .stream_position()
            .map_err(|error| format!("failed to scan MPD: {error}"))?;
        if position >= file_len {
            break;
        }
        progress(
            0.03 + 0.09 * position as f64 / file_len as f64,
            "scan_container",
            position.min(usize::MAX as u64) as usize,
            file_len.min(usize::MAX as u64) as usize,
        );
        let mut entry_header = [0u8; 8];
        reader
            .read_exact(&mut entry_header)
            .map_err(|error| format!("truncated MPD entry header: {error}"))?;
        let name_length = u32::from_le_bytes(entry_header[..4].try_into().unwrap()) as usize;
        if name_length > 1024 * 1024 {
            return Err("invalid MPD entry name length".to_string());
        }
        let mut name = vec![0u8; name_length];
        reader
            .read_exact(&mut name)
            .map_err(|error| format!("truncated MPD entry name: {error}"))?;
        let after_name = reader
            .stream_position()
            .map_err(|error| format!("failed to scan MPD: {error}"))? as usize;
        let aligned = align_8(after_name)? as u64;
        reader
            .seek(SeekFrom::Start(aligned))
            .map_err(|error| format!("failed to seek MPD entry: {error}"))?;
        let mut length_bytes = [0u8; 8];
        reader
            .read_exact(&mut length_bytes)
            .map_err(|error| format!("truncated MPD entry length: {error}"))?;
        let stored_length = u64::from_le_bytes(length_bytes);
        let payload_start = reader
            .stream_position()
            .map_err(|error| format!("failed to scan MPD: {error}"))?;
        let payload_end = payload_start
            .checked_add(stored_length)
            .ok_or_else(|| "MPD payload length overflow".to_string())?;
        if payload_end > file_len {
            return Err("truncated MPD entry payload".to_string());
        }
        let entry_name = String::from_utf8_lossy(&name).to_ascii_lowercase();
        if entry_name.contains("melodyne.graph") {
            if stored_length > MAX_DECODED_ENTRY_BYTES.saturating_add(20) {
                return Err("compressed MPD graph exceeds the input limit".to_string());
            }
            let mut compression_header = [0u8; 20];
            reader
                .read_exact(&mut compression_header)
                .map_err(|error| format!("truncated MPD graph header: {error}"))?;
            if compression_header.get(..8) == Some(COMPRESSED_MAGIC.as_slice()) {
                let compressed_length = u32::from_le_bytes(
                    compression_header[16..20].try_into().unwrap(),
                ) as u64;
                if compressed_length != stored_length.saturating_sub(20) {
                    return Err("invalid compressed MPD graph length".to_string());
                }
                progress(0.13, "decompress_graph", 0, 1);
                let mut decoder = ZlibDecoder::new(reader.take(compressed_length));
                let mut decoded = Vec::new();
                decoder
                    .by_ref()
                    .take(MAX_DECODED_ENTRY_BYTES + 1)
                    .read_to_end(&mut decoded)
                    .map_err(|error| format!("failed to decompress MPD graph: {error}"))?;
                if decoded.len() as u64 > MAX_DECODED_ENTRY_BYTES {
                    return Err("MPD graph entry exceeds the decode limit".to_string());
                }
                progress(0.22, "parse_graph", 1, 1);
                return Ok(decoded);
            }

            reader
                .seek(SeekFrom::Start(payload_start))
                .map_err(|error| format!("failed to seek MPD graph: {error}"))?;
            let mut decoded = Vec::with_capacity(stored_length as usize);
            reader
                .take(stored_length)
                .read_to_end(&mut decoded)
                .map_err(|error| format!("failed to read MPD graph: {error}"))?;
            progress(0.22, "parse_graph", 1, 1);
            return Ok(decoded);
        }
        reader
            .seek(SeekFrom::Start(payload_end))
            .map_err(|error| format!("failed to seek next MPD entry: {error}"))?;
    }
    Err("Melodyne project contains no object graph".to_string())
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
        if self.class_name(function_id) == Some("MUBezierDataPointFunction") {
            let Some(points_id) = self.reference(function_id, "points") else {
                return x;
            };
            let points = self.list(points_id);
            if points.is_empty() {
                return x;
            }
            let first = points[0];
            let first_x = self.f64(first, "x").unwrap_or(0.0);
            if x <= first_x || points.len() == 1 {
                return self.f64(first, "y").unwrap_or(x);
            }
            for pair in points.windows(2) {
                let left = pair[0];
                let right = pair[1];
                let x0 = self.f64(left, "x").unwrap_or(0.0);
                let y0 = self.f64(left, "y").unwrap_or(x0);
                let x3 = self.f64(right, "x").unwrap_or(x0);
                let y3 = self.f64(right, "y").unwrap_or(y0);
                if x > x3 {
                    continue;
                }
                let x1 = x0 + self.f64(left, "rightControlPointXDelta").unwrap_or(0.0);
                let y1 = y0 + self.f64(left, "rightControlPointYDelta").unwrap_or(0.0);
                let x2 = x3 + self.f64(right, "leftControlPointXDelta").unwrap_or(0.0);
                let y2 = y3 + self.f64(right, "leftControlPointYDelta").unwrap_or(0.0);
                // Solve Bezier X(t)=x. Melodyne marks this function invertible,
                // therefore a bounded binary search reproduces its mapping
                // without flattening the stored attack slope.
                let mut lo = 0.0f64;
                let mut hi = 1.0f64;
                for _ in 0..28 {
                    let t = (lo + hi) * 0.5;
                    let u = 1.0 - t;
                    let bx = u * u * u * x0
                        + 3.0 * u * u * t * x1
                        + 3.0 * u * t * t * x2
                        + t * t * t * x3;
                    if bx < x { lo = t; } else { hi = t; }
                }
                let t = (lo + hi) * 0.5;
                let u = 1.0 - t;
                return u * u * u * y0
                    + 3.0 * u * u * t * y1
                    + 3.0 * u * t * t * y2
                    + t * t * t * y3;
            }
            return points
                .last()
                .and_then(|point| self.f64(*point, "y"))
                .unwrap_or(x);
        }
        let points = self.function_points(function_id);
        interpolate_function_points(&points, x).unwrap_or(x)
    }

    fn inverse_eval_function(&self, function_id: u32, y: f64) -> Option<f64> {
        if !y.is_finite() {
            return None;
        }
        let points = self.function_points(function_id);
        let (first, last) = (*points.first()?, *points.last()?);
        let ascending = last.1 >= first.1;
        let mut lo = first.0;
        let mut hi = last.0;
        for _ in 0..32 {
            let x = (lo + hi) * 0.5;
            let value = self.eval_function(function_id, x);
            if (value < y) == ascending { lo = x; } else { hi = x; }
        }
        Some(((lo + hi) * 0.5).clamp(first.0.min(last.0), first.0.max(last.0)))
    }

    fn sampled_function_points(&self, function_id: u32) -> Vec<(f64, f64)> {
        let anchors = self.function_points(function_id);
        let (Some(first), Some(last)) = (anchors.first(), anchors.last()) else {
            return anchors;
        };
        // Two-millisecond subdivisions retain rapid attack/body ratio changes
        // from MPD's Bezier function. The renderer consumes every subdivision
        // through one persistent state, so the extra precision adds no phase
        // resets at the consonant/vowel handle.
        let steps = (((last.0 - first.0).abs() / 0.002).ceil() as usize).clamp(2, 2048);
        (0..=steps)
            .map(|index| {
                let fraction = index as f64 / steps as f64;
                let x = first.0 + (last.0 - first.0) * fraction;
                (x, self.eval_function(function_id, x))
            })
            .collect()
    }
}

fn interpolate_function_points(points: &[(f64, f64)], x: f64) -> Option<f64> {
        if points.is_empty() { return None; }
        if points.len() == 1 {
            return Some(points[0].1);
        }
        if x <= points[0].0 {
            return Some(points[0].1);
        }
        for pair in points.windows(2) {
            if x <= pair[1].0 {
                let span = (pair[1].0 - pair[0].0).max(1e-9);
                let fraction = ((x - pair[0].0) / span).clamp(0.0, 1.0);
                // A linear interpolation is deliberate here.  It keeps the
                // exact endpoints of Melodyne's Bezier warp and gives stable
                // transport in HachiShifter's uniform-rate clip engine.
                return Some(pair[0].1 + (pair[1].1 - pair[0].1) * fraction);
            }
        }
        points.last().map(|point| point.1)
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

fn build_media_index(project_dir: &Path) -> HashMap<String, PathBuf> {
    let mut index = HashMap::new();
    let Ok(entries) = fs::read_dir(project_dir) else {
        return index;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_file() {
            if let Some(name) = path.file_name().and_then(|name| name.to_str()) {
                index
                    .entry(name.to_lowercase())
                    .or_insert_with(|| fs::canonicalize(&path).unwrap_or(path));
            }
        } else if path.is_dir() {
            if let Ok(children) = fs::read_dir(&path) {
                for child in children.flatten() {
                    let child_path = child.path();
                    if child_path.is_file() {
                        if let Some(name) = child_path.file_name().and_then(|name| name.to_str()) {
                            index.entry(name.to_lowercase()).or_insert_with(|| {
                                fs::canonicalize(&child_path).unwrap_or(child_path)
                            });
                        }
                    }
                }
            }
        }
    }
    index
}

fn resolve_media_path(
    stored: &str,
    project_dir: &Path,
    media_index: &HashMap<String, PathBuf>,
) -> Option<PathBuf> {
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

    media_index.get(&file_name.to_lowercase()).cloned()
}

#[derive(Clone, Debug)]
struct SourceInfo {
    stored_path: String,
}

#[derive(Clone, Debug)]
struct ImportedElement {
    element_id: u32,
    following_element_id: Option<u32>,
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
    sibilant_balance: f32,
    attack_duration: f64,
    attack_time_slope: f64,
    decay_elongation: f32,
    anchor_point: f64,
    muted: bool,
    join_next: bool,
    join_phase_next: bool,
    join_duration: f64,
    fade_in_sec: f64,
    fade_out_sec: f64,
    fade_in_shape_pow: f32,
    fade_out_shape_pow: f32,
    join_amplitude: bool,
    amplitude_transition_duration: f64,
}

#[derive(Clone, Debug)]
struct ImportedTrack {
    name: String,
    muted: bool,
    solo: bool,
    melodic: bool,
    volume: f32,
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

fn track_volume(graph: &Graph, track_id: u32) -> f32 {
    let direct = graph.f32(track_id, "volume");
    let effect_fader = graph
        .reference(track_id, "effectChain")
        .and_then(|chain| graph.reference(chain, "effects"))
        .into_iter()
        .flat_map(|effects| graph.list(effects))
        .find(|effect| graph.class_name(*effect) == Some("MUFader"))
        .and_then(|fader| graph.f32(fader, "volume"));
    direct.or(effect_fader).unwrap_or(1.0).clamp(0.0, 4.0)
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
                let source_function_id =
                    graph.reference(element_id, "sourceTimeForElementTimeFunction");
                let source_sample_rate = graph
                    .reference(source_description_id, "audioSource")
                    .and_then(|source| graph.f64(source, "sampleRate"))
                    .unwrap_or(44_100.0)
                    .max(1.0);
                let source_item_origin_sec = graph
                    .i64(source_item_id, "startSampleIndex")
                    .or_else(|| graph.i32(source_item_id, "startSampleIndex").map(i64::from))
                    .unwrap_or(0)
                    .max(0) as f64
                    / source_sample_rate;
                let source_handle_to_element_time = |absolute_source_time: f64| {
                    // MPD persists these handles in the audio-description
                    // clock. The element time function, however, is local to
                    // its MUAudioSourcePrincipalItem. Passing the absolute
                    // value directly made every non-zero startSampleIndex
                    // produce an overlong fade-in or a missing fade-out.
                    let item_local_source_time =
                        (absolute_source_time - source_item_origin_sec).max(0.0);
                    source_function_id
                        .and_then(|function| {
                            graph.inverse_eval_function(function, item_local_source_time)
                        })
                        .unwrap_or(item_local_source_time)
                };
                // Melodyne 5 keeps fadeInTime/fadeOutTime at zero for most
                // edited notes and persists the actual fade handles in source
                // time. A plain Option fallback therefore discarded nearly
                // every crossfade. Prefer a positive explicit duration, then
                // derive it from the warped source-time endpoints.
                let fade_in_sec = graph
                    .f64(element_id, "fadeInTime")
                    .filter(|value| value.is_finite() && *value > 1e-9)
                    .unwrap_or_else(|| {
                        graph
                            .f64(element_id, "amplitudeFadeInEndSourceTime")
                            .filter(|value| value.is_finite())
                            .map(source_handle_to_element_time)
                            .unwrap_or(0.0)
                    })
                    .clamp(0.0, duration);
                let fade_out_sec = graph
                    .f64(element_id, "fadeOutTime")
                    .filter(|value| value.is_finite() && *value > 1e-9)
                    .unwrap_or_else(|| {
                        graph
                            .f64(element_id, "amplitudeFadeOutStartSourceTime")
                            .filter(|value| value.is_finite())
                            .map(source_handle_to_element_time)
                            .map(|start| duration - start)
                            .unwrap_or(0.0)
                    })
                    .clamp(0.0, duration);
                elements.push(ImportedElement {
                    element_id,
                    following_element_id: following_join
                        .and_then(|join| graph.reference(join, "followingElement")),
                    start,
                    duration,
                    source_id,
                    source_item_id,
                    source_description_id,
                    source_function_id,
                    pitch_center: graph.f32(element_id, "pitchCenter").unwrap_or(0.0),
                    pitch_drift: graph.f32(element_id, "pitchDriftFactor").unwrap_or(1.0),
                    pitch_modulation: graph
                        .f32(element_id, "pitchModulationFactor")
                        .unwrap_or(1.0),
                    formant_offset: graph.f32(element_id, "formantOffset").unwrap_or(0.0),
                    amplitude_factor: graph.f32(element_id, "amplitudeFactor").unwrap_or(1.0),
                    sibilant_balance: graph.f32(element_id, "sibilantBalance").unwrap_or(0.0),
                    attack_duration: graph.f64(element_id, "attackDuration").unwrap_or(0.0),
                    attack_time_slope: graph
                        .f64(element_id, "sourceTimeForElementTimeFunctionAttackSlope")
                        .unwrap_or(1.0),
                    decay_elongation: graph.f32(element_id, "decayElongation").unwrap_or(0.0),
                    anchor_point: graph.f64(element_id, "anchorPoint").unwrap_or(0.0),
                    muted: graph.bool(element_id, "isMuted"),
                    join_next: following_join
                        .map(|join| graph.bool(join, "joinsPitches"))
                        .unwrap_or(false),
                    join_phase_next: following_join
                        .map(|join| graph.bool(join, "joinsPhases"))
                        .unwrap_or(false),
                    join_duration: following_join
                        .and_then(|join| graph.f64(join, "pitchTransitionDuration"))
                        .unwrap_or(0.0),
                    fade_in_sec,
                    fade_out_sec,
                    fade_in_shape_pow: graph
                        .f64(element_id, "amplitudeFadeInShapePow")
                        .unwrap_or(1.0)
                        .clamp(0.1, 8.0) as f32,
                    fade_out_shape_pow: graph
                        .f64(element_id, "amplitudeFadeOutShapePow")
                        .unwrap_or(1.0)
                        .clamp(0.1, 8.0) as f32,
                    join_amplitude: following_join.map(|join| graph.bool(join, "joinsAmplitudes")).unwrap_or(false),
                    amplitude_transition_duration: following_join.and_then(|join| graph.f64(join, "amplitudeTransitionDuration")).unwrap_or(0.0).max(0.0),
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
                volume: track_volume(graph, track_id),
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

fn integer_field(graph: &Graph, object_id: u32, field: &str) -> Option<i64> {
    graph
        .i64(object_id, field)
        .or_else(|| graph.i32(object_id, field).map(i64::from))
}

/// Reconstruct Melodyne's analysed sibilant component from the persisted
/// principal-item boundaries.  Melodyne keeps leading and trailing sibilants
/// outside the periodic body so pitch/time processing never turns /s/, /sh/
/// or /ch/ noise into pitched, hoarse harmonics.
fn source_sibilant_strength(
    graph: &Graph,
    element: &ImportedElement,
    local_time: f64,
) -> f32 {
    let source_id = graph
        .reference(element.source_description_id, "audioSource");
    let sample_rate = source_id
        .and_then(|source| graph.f64(source, "sampleRate"))
        .unwrap_or(44_100.0)
        .max(1.0);
    let item_start = integer_field(graph, element.source_item_id, "startSampleIndex")
        .unwrap_or(0)
        .max(0) as f64;
    let item_samples = integer_field(graph, element.source_item_id, "sampleCount")
        .unwrap_or(0)
        .max(0) as f64;
    if item_samples <= 1.0 {
        return 0.0;
    }
    let leading_end = integer_field(
        graph,
        element.source_item_id,
        "startSibilantEndSampleOffset",
    )
    .unwrap_or(0)
    .clamp(0, item_samples as i64) as f64;
    let trailing_start = integer_field(
        graph,
        element.source_item_id,
        "endSibilantStartSampleOffset",
    )
    .unwrap_or(item_samples as i64)
    .clamp(0, item_samples as i64) as f64;
    let mapped_sample = source_time(graph, element, local_time) * sample_rate - item_start;
    if !mapped_sample.is_finite() {
        return 0.0;
    }
    // Melodyne displays a soft component boundary rather than a binary gate.
    // A 4-ms shoulder avoids introducing a new click at the sibilant/body
    // split while retaining the exact stored sample offsets at its centre.
    let shoulder = (sample_rate * 0.004).max(1.0);
    let leading = if leading_end > 0.0 {
        ((leading_end + shoulder - mapped_sample) / shoulder).clamp(0.0, 1.0)
    } else {
        0.0
    };
    let trailing = if trailing_start < item_samples {
        ((mapped_sample - trailing_start + shoulder) / shoulder).clamp(0.0, 1.0)
    } else {
        0.0
    };
    leading.max(trailing) as f32
}

fn element_sibilant_handles(
    graph: &Graph,
    element: &ImportedElement,
    shift: f64,
) -> (Option<f64>, Option<f64>) {
    let Some(source_id) = graph.reference(element.source_description_id, "audioSource") else {
        return (None, None);
    };
    let sample_rate = graph.f64(source_id, "sampleRate").unwrap_or(44_100.0).max(1.0);
    let item_samples = integer_field(graph, element.source_item_id, "sampleCount")
        .unwrap_or(0)
        .max(0) as f64;
    if item_samples <= 1.0 {
        return (None, None);
    }
    let to_element_time = |source_offset_samples: f64| {
        let source_local = source_offset_samples / sample_rate;
        element
            .source_function_id
            .and_then(|function| graph.inverse_eval_function(function, source_local))
            .unwrap_or(source_local)
            .clamp(0.0, element.duration)
    };
    let leading = integer_field(
        graph,
        element.source_item_id,
        "startSibilantEndSampleOffset",
    )
    .filter(|offset| *offset > 0)
    .map(|offset| element.start + shift + to_element_time(offset as f64));
    let trailing = integer_field(
        graph,
        element.source_item_id,
        "endSibilantStartSampleOffset",
    )
    .filter(|offset| *offset >= 0 && (*offset as f64) < item_samples)
    .map(|offset| element.start + shift + to_element_time(offset as f64));
    (leading, trailing)
}

fn element_time_map_points(
    graph: &Graph,
    element: &ImportedElement,
    shift: f64,
) -> Vec<crate::state::MelodyneTimeMapPoint> {
    let mut points = element
        .source_function_id
        .map(|function| graph.sampled_function_points(function))
        .unwrap_or_default();
    points.push((
        0.0,
        element
            .source_function_id
            .map(|function| graph.eval_function(function, 0.0))
            .unwrap_or(0.0),
    ));
    points.push((
        element.duration,
        element
            .source_function_id
            .map(|function| graph.eval_function(function, element.duration))
            .unwrap_or(element.duration),
    ));
    // `attackDuration` is the destination consonant/vowel handle; preserving
    // it exactly avoids moving the boundary to the nearest sampled point.
    if element.attack_duration > 0.0 && element.attack_duration < element.duration {
        points.push((
            element.attack_duration,
            element
                .source_function_id
                .map(|function| graph.eval_function(function, element.attack_duration))
                .unwrap_or(element.attack_duration),
        ));
    }
    points.sort_by(|left, right| left.0.total_cmp(&right.0));
    points.dedup_by(|left, right| (left.0 - right.0).abs() <= 1e-7);
    points
        .into_iter()
        .filter(|(time, source)| {
            time.is_finite()
                && source.is_finite()
                && *time >= 0.0
                && *time <= element.duration
        })
        .map(|(time, source)| crate::state::MelodyneTimeMapPoint {
            timeline_sec: element.start + shift + time,
            // sampled_function_points returns function-local source time;
            // source_time additionally applies the audio-item origin.
            source_sec: {
                let function_origin = element
                    .source_function_id
                    .map(|function| graph.eval_function(function, 0.0))
                    .unwrap_or(0.0)
                    .max(0.0);
                let item_origin = source_time(graph, element, 0.0) - function_origin;
                item_origin + source.max(0.0)
            },
        })
        .collect()
}

fn register_project_note_objects(
    graph: &Graph,
    tracks: &[ImportedTrack],
    resolved_sources: &BTreeMap<u32, Option<PathBuf>>,
    compose_tracks: Option<&HashSet<usize>>,
) {
    use crate::sample_annotations::{
        NoteDetectorKind, SampleAnalysis, SamplePitchNote, SampleRegionAnnotation,
    };

    for (source_id, resolved) in resolved_sources {
        let Some(path) = resolved.as_ref() else { continue; };
        let mut rows = Vec::new();
        let mut notes = Vec::new();
        for (track_index, track) in tracks.iter().enumerate() {
            if compose_tracks.is_some_and(|selected| !selected.contains(&track_index)) {
                continue;
            }
            for element in track.elements.iter().filter(|item| item.source_id == *source_id) {
                let start = source_time(graph, element, 0.0).max(0.0);
                let end = source_time(graph, element, element.duration).max(start + 0.001);
                // The persisted anchor is Melodyne's timing handle within the
                // note object. Clamp it into the source region because older
                // graph versions occasionally retain an out-of-range value.
                let anchor_local = element.anchor_point.clamp(0.0, element.duration);
                let alignment = source_time(graph, element, anchor_local).clamp(start, end);
                let original_center = graph
                    .f32(element.source_item_id, "pitchCenter")
                    .unwrap_or(element.pitch_center);

                // Reused elements can occur on more than one composition
                // track. The source editor needs one source-time boundary.
                if rows.iter().any(|row: &SampleRegionAnnotation| {
                    (row.region_start_sec - start).abs() < 0.0005
                        && (row.region_end_sec - end).abs() < 0.0005
                }) {
                    continue;
                }
                let index = rows.len() + 1;
                rows.push(SampleRegionAnnotation {
                    name: format!("Melodyne {index}"),
                    region_start_sec: start,
                    region_end_sec: end,
                    note_alignment_sec: alignment,
                    fixed_duration_sec: (alignment - start).max(0.0),
                    relative_pitch_cents: 0.0,
                    melodyne_project_data: true,
                    melodyne_pitch_center_cents: element.pitch_center as f64,
                    melodyne_original_pitch_center_cents: original_center as f64,
                    melodyne_pitch_drift_factor: element.pitch_drift as f64,
                    melodyne_pitch_modulation_factor: element.pitch_modulation as f64,
                    melodyne_transition_sec: element.join_duration.max(0.0),
                    melodyne_formant_offset_cents: element.formant_offset as f64,
                    melodyne_amplitude_factor: element.amplitude_factor as f64,
                    melodyne_sibilant_balance: element.sibilant_balance as f64,
                    melodyne_attack_duration_sec: element.attack_duration.max(0.0),
                    melodyne_decay_elongation: element.decay_elongation as f64,
                });
                notes.push(SamplePitchNote {
                    start_sec: start,
                    end_sec: end,
                    midi_note: (element.pitch_center / 100.0).clamp(0.0, 127.0),
                    confidence: 1.0,
                });
            }
        }
        if rows.is_empty() { continue; }
        let mut paired: Vec<_> = rows.into_iter().zip(notes).collect();
        paired.sort_by(|left, right| left.0.region_start_sec.total_cmp(&right.0.region_start_sec));
        let (annotations, pitch_notes) = paired.into_iter().unzip();
        crate::sample_annotations::register_melodyne_project_analysis(
            path,
            SampleAnalysis {
                annotations,
                pitch_notes,
                audio_events: Vec::new(),
                note_detector: NoteDetectorKind::Melodyne,
                detector_message: Some("Restored from Melodyne project note objects".to_string()),
            },
        );
    }
}

#[derive(Clone, Debug)]
struct ClipGroup {
    source_id: u32,
    placement: f64,
    element_count: usize,
    element_indices: Vec<usize>,
    timeline_start: f64,
    timeline_end: f64,
    source_start: f64,
    source_end: f64,
}

fn group_clips(graph: &Graph, track: &ImportedTrack) -> Vec<ClipGroup> {
    let mut groups: Vec<ClipGroup> = Vec::new();
    let mut placement_bins: HashMap<(u32, i64), Vec<usize>> = HashMap::new();
    for (element_index, element) in track.elements.iter().enumerate() {
        let source_start = source_time(graph, element, 0.0);
        let source_end = source_time(graph, element, element.duration).max(source_start + 1e-6);
        let placement = element.start - source_start;
        // Elements from one source occurrence share a placement.  A 50 ms
        // tolerance retains manually warped notes while keeping a later reuse
        // of the same sample as a distinct clip.
        let bin = (placement / 0.05).round() as i64;
        let matching_index = (-1i64..=1).find_map(|delta| {
            placement_bins
                .get(&(element.source_id, bin + delta))
                .and_then(|indices| {
                    indices.iter().copied().find(|group_index| {
                        (groups[*group_index].placement - placement).abs() <= 0.05
                    })
                })
        });
        if let Some(group_index) = matching_index {
            let group = &mut groups[group_index];
            group.element_count += 1;
            group.element_indices.push(element_index);
            group.timeline_start = group.timeline_start.min(element.start);
            group.timeline_end = group.timeline_end.max(element.start + element.duration);
            group.source_start = group.source_start.min(source_start);
            group.source_end = group.source_end.max(source_end);
            let count = group.element_count as f64;
            group.placement += (placement - group.placement) / count;
        } else {
            let group_index = groups.len();
            groups.push(ClipGroup {
                source_id: element.source_id,
                placement,
                element_count: 1,
                element_indices: vec![element_index],
                timeline_start: element.start,
                timeline_end: element.start + element.duration,
                source_start,
                source_end,
            });
            placement_bins
                .entry((element.source_id, bin))
                .or_default()
                .push(group_index);
        }
    }
    groups.sort_by(|left, right| left.timeline_start.total_cmp(&right.timeline_start));
    groups
}

#[derive(Clone, Copy)]
struct CachedPitchPoint {
    slice: f64,
    pitch: f32,
    without_vibrato: f32,
    silent: bool,
}

struct SourcePitchCache {
    item_start: f64,
    time_slice_count: f64,
    slices_per_second: f64,
    points: Vec<CachedPitchPoint>,
}

fn build_source_pitch_cache(
    graph: &Graph,
    element: &ImportedElement,
) -> Option<SourcePitchCache> {
    let property_points = graph.reference(element.source_item_id, "propertyPoints")?;
    let point_ids = graph.list(property_points);
    let source_id = graph.reference(element.source_description_id, "audioSource")?;
    let sample_rate = graph.f64(source_id, "sampleRate")?.max(1.0);
    let mut points = Vec::with_capacity(point_ids.len());
    for (index, point_id) in point_ids.into_iter().enumerate() {
        let Some(pitch) = graph.f32(point_id, "pitchCent") else {
            continue;
        };
        points.push(CachedPitchPoint {
            slice: graph
                .i32(point_id, "timeSliceIndex")
                .unwrap_or(index as i32) as f64,
            pitch,
            without_vibrato: graph
                .f32(point_id, "pitchWithoutVibrato")
                .unwrap_or(pitch),
            silent: graph.bool(point_id, "isConsideredSilent"),
        });
    }
    points.sort_by(|left, right| left.slice.total_cmp(&right.slice));
    // `timeSliceIndex` addresses the source description's analysis frames.
    // `parameterValuesPerSecond` is the density of values stored inside a
    // slice (441 in this MPD5 fixture), not the slice clock.  Treating it as
    // Hz advances the source F0 by about 10.24x and selects unrelated notes.
    // Melodyne 5 advances this analysis on an exact 1024-sample hop. Deriving
    // `(count - 1) / duration` looks close for a long file, but is biased by
    // the final partial slice on short vocal samples (often by 3-8%). Item
    // start indices in the MPD independently confirm floor(sample / 1024).
    let time_slice_count = graph
        .i32(element.source_description_id, "timeSliceCount")
        .unwrap_or(points.len() as i32)
        .max(1) as f64;
    let slices_per_second = sample_rate / 1024.0;
    Some(SourcePitchCache {
        item_start: graph
            .i64(element.source_item_id, "startSampleIndex")
            .unwrap_or(0)
            .max(0) as f64
            / sample_rate,
        time_slice_count,
        slices_per_second,
        points,
    })
}

fn source_pitch_at(
    graph: &Graph,
    cache: &SourcePitchCache,
    time_function: Option<u32>,
    local_time: f64,
) -> Option<(f32, f32)> {
    if cache.points.is_empty() {
        return None;
    }
    let mapped_time = time_function
        .map(|function| graph.eval_function(function, local_time))
        .unwrap_or(local_time)
        .max(0.0);
    let slice_position = (cache.item_start + mapped_time) * cache.slices_per_second;
    let first_slice = cache.points.first()?.slice;
    let last_slice = cache.points.last()?.slice;
    // Each stored property point represents the analysis cell centred on its
    // 1024-sample slice. Melodyne stops drawing/using that contour at the
    // midpoint to the absent neighbouring cell. Extending the nearest point
    // through the remainder of a stretched attack/tail created the long,
    // flat or sharply falling F0 ends visible in HachiShifter.
    const PROPERTY_POINT_HALF_CELL: f64 = 0.5 + 1e-7;
    if slice_position < first_slice - PROPERTY_POINT_HALF_CELL
        || slice_position > last_slice + PROPERTY_POINT_HALF_CELL
    {
        return None;
    }
    let slice_position = slice_position
        .clamp(0.0, (cache.time_slice_count - 1.0).max(0.0));
    // The former linear scan made long, densely analysed sources quadratic:
    // every 5 ms output frame walked the complete source point list.  Melodyne
    // stores points sorted by time slice, so a binary partition is sufficient.
    let right_index = cache
        .points
        .partition_point(|point| point.slice < slice_position)
        .min(cache.points.len() - 1);
    let left_index = right_index
        .saturating_sub(usize::from(cache.points[right_index].slice > slice_position));
    let left = cache.points[left_index];
    let right = cache.points[right_index];
    let fraction = if right.slice > left.slice {
        ((slice_position - left.slice) / (right.slice - left.slice)).clamp(0.0, 1.0) as f32
    } else {
        0.0
    };
    // `isConsideredSilent` is a voicing/property validity flag, not merely an
    // amplitude hint. Interpolating from a silent attack/tail point to a
    // pitched vowel fabricated the large falling/rising curves visible in
    // HachiShifter but absent in Melodyne. A validity boundary must split the
    // contour; it must never be bridged by linear or cubic interpolation.
    if left.silent || right.silent {
        return None;
    }
    // Melodyne's stored analysis points run on a 1024-sample clock, while its
    // display/playback functions are evaluated continuously.  A straight
    // line between the 43-Hz property points visibly polygonises vibrato and
    // also gives the vocoder a staircase derivative.  Evaluate a bounded
    // cubic Hermite segment instead: it retains every persisted point exactly
    // and reconstructs the dense display/playback cache without overshooting
    // neighbouring extrema or interpolating across silent regions.
    let interpolate = |value: fn(CachedPitchPoint) -> f32| {
        if right_index == left_index {
            return value(left) + (value(right) - value(left)) * fraction;
        }
        let p0 = cache.points[left_index.saturating_sub(1)];
        let p3 = cache.points[(right_index + 1).min(cache.points.len() - 1)];
        if p0.silent || p3.silent {
            return value(left) + (value(right) - value(left)) * fraction;
        }
        let y0 = value(p0);
        let y1 = value(left);
        let y2 = value(right);
        let y3 = value(p3);
        let x = fraction;
        let x2 = x * x;
        let x3 = x2 * x;
        let m1 = 0.5 * (y2 - y0);
        let m2 = 0.5 * (y3 - y1);
        let cubic = (2.0 * x3 - 3.0 * x2 + 1.0) * y1
            + (x3 - 2.0 * x2 + x) * m1
            + (-2.0 * x3 + 3.0 * x2) * y2
            + (x3 - x2) * m2;
        cubic.clamp(y0.min(y1).min(y2).min(y3), y0.max(y1).max(y2).max(y3))
    };
    Some((
        interpolate(|point| point.pitch),
        interpolate(|point| point.without_vibrato),
    ))
}

fn build_track_params(
    graph: &Graph,
    track: &ImportedTrack,
    shift: f64,
    project_sec: f64,
    frame_period_ms: f64,
    track_index: usize,
    track_count: usize,
    progress: &ImportProgress<'_>,
) -> TrackParamsState {
    let frame_period_sec = frame_period_ms / 1000.0;
    let frame_count = (project_sec / frame_period_sec).ceil().max(1.0) as usize + 1;
    let mut pitch_orig = vec![0.0f32; frame_count];
    let mut pitch_edit = vec![0.0f32; frame_count];
    let mut pitch_without_vibrato = vec![0.0f32; frame_count];
    let has_formant = track
        .elements
        .iter()
        .any(|element| element.formant_offset.abs() > 1e-6);
    let has_sibilant = track.elements.iter().any(|element| element.sibilant_balance.abs() > 1e-6);
    let mut formant = has_formant.then(|| vec![0.0f32; frame_count]);
    let mut sibilant = has_sibilant.then(|| vec![0.0f32; frame_count]);
    let mut sibilant_mask = vec![0.0f32; frame_count];
    // A source item is commonly referenced by hundreds of Melodyne elements.
    // Decode its property-point list once instead of cloning it per note.
    let mut pitch_cache_by_item: HashMap<u32, Option<SourcePitchCache>> = HashMap::new();

    for (element_index, element) in track.elements.iter().enumerate() {
        if element_index % 64 == 0 {
            let within = element_index as f64 / track.elements.len().max(1) as f64;
            progress(
                0.65 + 0.28 * (track_index as f64 + within) / track_count.max(1) as f64,
                "restore_edits",
                element_index,
                track.elements.len(),
            );
        }
        if element.muted {
            continue;
        }
        pitch_cache_by_item
            .entry(element.source_item_id)
            .or_insert_with(|| build_source_pitch_cache(graph, element));
        let pitch_cache = pitch_cache_by_item
            .get(&element.source_item_id)
            .and_then(Option::as_ref);
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
            let sibilant_strength = source_sibilant_strength(graph, element, local_time);
            sibilant_mask[frame] = sibilant_mask[frame].max(sibilant_strength);
            // Formant and sibilant edits belong to independent components and
            // remain meaningful even where Melodyne has no reliable periodic
            // F0 line.
            if let Some(curve) = formant.as_mut() {
                curve[frame] = element.formant_offset;
            }
            if let Some(curve) = sibilant.as_mut() {
                curve[frame] = element.sibilant_balance;
            }
            if sibilant_strength >= 0.55 {
                // Sibilants are an independent aperiodic component. Giving
                // them an interpolated pitch target creates the long false F0
                // tails seen around attack and sample boundaries.
                continue;
            }
            let Some((raw, without_vibrato)) = pitch_cache.and_then(|cache| {
                source_pitch_at(graph, cache, element.source_function_id, local_time)
            }) else {
                // Zero is an explicit "no reliable periodic contour" marker.
                // Do not manufacture a flat source-center line through a
                // silent attack, breath, sibilant or unanalysed tail.
                continue;
            };
            let edited = element.pitch_center
                + element.pitch_drift * (without_vibrato - source_center)
                + element.pitch_modulation * (raw - without_vibrato);
            pitch_orig[frame] = (raw / 100.0).clamp(0.0, 127.0);
            pitch_without_vibrato[frame] = (without_vibrato / 100.0).clamp(0.0, 127.0);
            pitch_edit[frame] = (edited / 100.0).clamp(0.0, 127.0);
        }
    }

    // Import the exact stored contours. Connections are intentionally left
    // untouched unless MPD explicitly persists joinsPitches. In that case the
    // stored pitchTransitionDuration belongs to the render, not merely the UI.
    // Blend only the note-centre offset and retain each side's local residual
    // contour, matching Melodyne's "move the line, do not flatten it" model.
    let unjoined_pitch = pitch_edit.clone();
    // This is the authoritative contour used by Disconnect and by manual
    // cross-sample joins.  A join is a reversible centre transition, never a
    // destructive low-pass pass over the detailed F0 line.
    let index_by_id = track
        .elements
        .iter()
        .enumerate()
        .map(|(index, element)| (element.element_id, index))
        .collect::<HashMap<_, _>>();
    for left in &track.elements {
        if !left.join_next || left.join_duration <= 1e-6 {
            continue;
        }
        let Some(right) = left
            .following_element_id
            .and_then(|id| index_by_id.get(&id).copied())
            .and_then(|index| track.elements.get(index))
        else {
            continue;
        };
        let left_end = left.start + left.duration + shift;
        let right_start = right.start + shift;
        let boundary = (left_end + right_start) * 0.5;
        let duration = left.join_duration
            .min(left.duration + right.duration)
            .max(frame_period_sec);
        let transition_start = (boundary - duration * 0.5)
            .max(left.start + shift)
            .max(0.0);
        let transition_end = (boundary + duration * 0.5)
            .min(right.start + right.duration + shift)
            .max(transition_start + frame_period_sec);
        let first = (transition_start / frame_period_sec).floor().max(0.0) as usize;
        let last = (transition_end / frame_period_sec).ceil().max(0.0) as usize;
        let left_probe = ((boundary - frame_period_sec) / frame_period_sec)
            .round().max(0.0) as usize;
        let right_probe = ((boundary + frame_period_sec) / frame_period_sec)
            .round().max(0.0) as usize;
        let left_center = unjoined_pitch.get(left_probe).copied().unwrap_or(0.0);
        let right_center = unjoined_pitch.get(right_probe).copied().unwrap_or(0.0);
        if left_center <= 0.0 || right_center <= 0.0 {
            continue;
        }
        for frame in first..last.min(pitch_edit.len()) {
            let time = frame as f64 * frame_period_sec;
            let x = ((time - transition_start)
                / (transition_end - transition_start).max(frame_period_sec))
                .clamp(0.0, 1.0) as f32;
            let smooth = x * x * (3.0 - 2.0 * x);
            let original = unjoined_pitch[frame];
            if original <= 0.0 {
                continue;
            }
            let local_center = if time < boundary { left_center } else { right_center };
            let residual = original - local_center;
            pitch_edit[frame] = left_center
                + (right_center - left_center) * smooth
                + residual;
        }
    }

    let mut extra_curves = HashMap::new();
    extra_curves.insert("mld5_pitch_unjoined".to_string(), unjoined_pitch);
    extra_curves.insert("mld5_pitch_without_vibrato".to_string(), pitch_without_vibrato);
    extra_curves.insert("mld5_sibilant_mask".to_string(), sibilant_mask);
    if let Some(curve) = formant {
        extra_curves.insert("formant_shift_cents".to_string(), curve);
    }
    if let Some(curve) = sibilant {
        extra_curves.insert("mld5_sibilant_balance".to_string(), curve);
    }
    TrackParamsState {
        frame_period_ms,
        pitch_orig,
        pitch_edit,
        pitch_edit_user_modified: true,
        extra_curves,
        ..TrackParamsState::default()
    }
}

fn build_group_pitch_curves(
    graph: &Graph,
    track: &ImportedTrack,
    group: &ClipGroup,
    shift: f64,
    frame_period_ms: f64,
) -> (Vec<f32>, Vec<f32>, Vec<f32>) {
    let frame_sec = frame_period_ms.max(0.1) / 1000.0;
    let clip_start = group.timeline_start + shift;
    let clip_length = (group.timeline_end - group.timeline_start).max(0.001);
    let curve_len = (clip_length / frame_sec).ceil() as usize + 2;
    let mut source_curve = vec![0.0f32; curve_len];
    let mut target_curve = vec![0.0f32; curve_len];
    let mut render_target_curve = vec![0.0f32; curve_len];
    let mut pitch_cache_by_item: HashMap<u32, Option<SourcePitchCache>> = HashMap::new();
    for element_index in &group.element_indices {
        let Some(element) = track.elements.get(*element_index) else {
            continue;
        };
        if element.muted {
            continue;
        }
        pitch_cache_by_item
            .entry(element.source_item_id)
            .or_insert_with(|| build_source_pitch_cache(graph, element));
        let pitch_cache = pitch_cache_by_item
            .get(&element.source_item_id)
            .and_then(Option::as_ref);
        let destination_start = element.start + shift;
        let first = ((destination_start - clip_start) / frame_sec).floor().max(0.0) as usize;
        let last = ((destination_start + element.duration - clip_start) / frame_sec)
            .ceil()
            .max(0.0) as usize;
        let source_center = graph
            .f32(element.source_item_id, "pitchCenter")
            .unwrap_or(element.pitch_center);
        for local_frame in first..last.min(source_curve.len()) {
            let destination_time = clip_start + local_frame as f64 * frame_sec;
            let element_time = destination_time - destination_start;
            if source_sibilant_strength(graph, element, element_time) >= 0.55 {
                continue;
            }
            if let Some((raw, without_vibrato)) =
                pitch_cache.and_then(|cache| {
                    source_pitch_at(graph, cache, element.source_function_id, element_time)
                })
            {
                let edited = element.pitch_center
                    + element.pitch_drift * (without_vibrato - source_center)
                    + element.pitch_modulation * (raw - without_vibrato);
                source_curve[local_frame] = (raw / 100.0).clamp(0.0, 127.0);
                target_curve[local_frame] = (edited / 100.0).clamp(0.0, 127.0);
                render_target_curve[local_frame] = (edited / 100.0).clamp(0.0, 127.0);
            } else {
                // Keep the display contour absent, but retain an element-
                // centre target for the renderer. WORLD applies this only
                // when its periodic detector still considers the frame
                // voiced, preventing a hidden attack/tail frame from jumping
                // back to the unedited source pitch.
                render_target_curve[local_frame] =
                    (element.pitch_center / 100.0).clamp(0.0, 127.0);
            }
        }
    }
    (source_curve, target_curve, render_target_curve)
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

fn choose_frame_period_ms(tracks: &[ImportedTrack], project_sec: f64) -> f64 {
    let bytes_per_project_frame: usize = tracks
        .iter()
        .filter(|track| track.melodic)
        .map(|track| {
            let has_formant = track
                .elements
                .iter()
                .any(|element| element.formant_offset.abs() > 1e-6);
            // pitch_orig + pitch_edit + unjoined edit + without-vibrato +
            // sibilant mask. Keep the reversible connection base inside the
            // same aggregate large-project budget.
            20 + usize::from(has_formant) * 4
                + usize::from(track.elements.iter().any(|element| element.sibilant_balance.abs() > 1e-6)) * 4
        })
        .sum();
    if bytes_per_project_frame == 0 || project_sec <= 0.0 {
        return IMPORT_FRAME_PERIOD_MS;
    }
    let estimated = project_sec * 1000.0 / IMPORT_FRAME_PERIOD_MS
        * bytes_per_project_frame as f64;
    if estimated <= MAX_IMPORT_PARAM_BYTES {
        IMPORT_FRAME_PERIOD_MS
    } else {
        // Round upward to a whole millisecond.  This places a hard aggregate
        // budget on dense HachiShifter curves while retaining Melodyne's
        // original property points in the graph during reconstruction.
        (IMPORT_FRAME_PERIOD_MS * estimated / MAX_IMPORT_PARAM_BYTES)
            .ceil()
            .max(IMPORT_FRAME_PERIOD_MS)
    }
}

fn import_flat_paths(path: &Path, data: &[u8]) -> Result<MelodyneImportResult, String> {
    let referenced_files = referenced_audio_paths(data)?;
    let project_dir = path.parent().unwrap_or_else(|| Path::new("."));
    let media_index = build_media_index(project_dir);
    let mut timeline = TimelineState::default();
    timeline.clips.clear();
    let mut missing_files = Vec::new();
    for (index, stored) in referenced_files.iter().enumerate() {
        let resolved = resolve_media_path(stored, project_dir, &media_index);
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

fn import_graph(
    path: &Path,
    graph: Graph,
    progress: &ImportProgress<'_>,
    compose_track_indices: Option<&[usize]>,
    reanalyze_pitch_with_game_fcpe: bool,
) -> Result<MelodyneImportResult, String> {
    progress(0.25, "read_tracks", 0, 1);
    let (tracks, sources) = collect_project(&graph)?;
    progress(0.34, "resolve_media", 0, sources.len());
    let referenced_files: Vec<String> = sources
        .values()
        .map(|source| source.stored_path.clone())
        .collect();
    let project_dir = path.parent().unwrap_or_else(|| Path::new("."));
    let media_index = build_media_index(project_dir);
    let resolved_sources: BTreeMap<u32, Option<PathBuf>> = sources
        .iter()
        .enumerate()
        .map(|(index, (source_id, source))| {
            if index % 32 == 0 {
                progress(0.34 + 0.08 * index as f64 / sources.len().max(1) as f64, "resolve_media", index, sources.len());
            }
            (
                *source_id,
                resolve_media_path(&source.stored_path, project_dir, &media_index),
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
        .enumerate()
        .map(|(index, track)| {
            progress(0.42 + 0.08 * index as f64 / tracks.len().max(1) as f64, "group_samples", index, tracks.len());
            group_clips(&graph, track)
        })
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
    let frame_period_ms = choose_frame_period_ms(&tracks, project_sec);
    let compose_tracks = compose_track_indices.map(|indices| indices.iter().copied().collect::<HashSet<_>>());

    let mut timeline = TimelineState::default();
    timeline.clips.clear();
    timeline.params_by_root_track.clear();
    timeline.tracks.clear();
    timeline.next_track_order = 0;

    for (track_index, (track, clip_groups)) in tracks.iter().zip(grouped.iter()).enumerate() {
        let compose = compose_tracks.as_ref().map_or(track.melodic, |selected| selected.contains(&track_index));
        progress(
            0.50 + 0.15 * track_index as f64 / tracks.len().max(1) as f64,
            "create_clips",
            track_index,
            tracks.len(),
        );
        let track_id = timeline.add_track(Some(track.name.clone()), None, None);
        if let Some(state_track) = timeline.tracks.iter_mut().find(|item| item.id == track_id) {
            state_track.muted = track.muted;
            state_track.solo = track.solo;
            state_track.volume = track.volume;
            state_track.compose_enabled = compose;
            state_track.pitch_analysis_algo = if compose { PitchAnalysisAlgo::Mld5 } else { PitchAnalysisAlgo::None };
        }
        let mut clip_pitch_curves =
            Vec::<(String, Vec<f32>, Vec<f32>, Vec<f32>)>::new();
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
                // MPD joins and source-time fade handles are rendered below
                // per element.  Do not manufacture clip-edge crossfades:
                // they double-apply level ramps, alter Melodyne's overlap
                // balance, and hide the persisted joinsPhases decision.
                clip.fade_in_sec = 0.0;
                clip.fade_out_sec = 0.0;
                clip.melodyne_warp_segments = group.element_indices.iter().filter_map(|index| {
                    let element = track.elements.get(*index)?;
                    let (leading_sibilant_end_sec, trailing_sibilant_start_sec) =
                        element_sibilant_handles(&graph, element, shift);
                    Some(crate::state::MelodyneWarpSegment {
                        melodyne_element_id: Some(element.element_id),
                        melodyne_following_element_id: element.following_element_id,
                        timeline_start_sec: element.start + shift,
                        timeline_end_sec: element.start + element.duration + shift,
                        source_start_sec: source_time(&graph, element, 0.0).max(0.0),
                        source_end_sec: source_time(&graph, element, element.duration)
                            .max(source_time(&graph, element, 0.0) + 0.001),
                        time_map_points: element_time_map_points(&graph, element, shift),
                        attack_duration_sec: element.attack_duration.max(0.0),
                        attack_source_sec: source_time(&graph, element, element.attack_duration),
                        attack_time_slope: element.attack_time_slope.max(1e-6),
                        leading_sibilant_end_sec,
                        trailing_sibilant_start_sec,
                        connected_to_next: element.join_next,
                        connected_to_clip_id: None,
                        connected_to_element_id: element.following_element_id,
                        pitch_transition_sec: element.join_duration.max(0.0),
                        connected_phase_to_next: element.join_phase_next,
                        amplitude_factor: element.amplitude_factor.max(0.0),
                        fade_in_sec: element.fade_in_sec,
                        fade_out_sec: element.fade_out_sec,
                        fade_in_shape_pow: element.fade_in_shape_pow,
                        fade_out_shape_pow: element.fade_out_shape_pow,
                        amplitude_transition_sec: element.amplitude_transition_duration,
                        connected_amplitude_to_next: element.join_amplitude,
                    })
                }).collect();
                if compose {
                    let (source_curve, target_curve, render_target_curve) =
                        build_group_pitch_curves(
                            &graph,
                            track,
                            group,
                            shift,
                            frame_period_ms,
                        );
                    clip_pitch_curves.push((
                        clip.id.clone(),
                        source_curve,
                        target_curve,
                        render_target_curve,
                    ));
                }
            }
        }
        if compose {
            let mut params = build_track_params(
                &graph,
                track,
                shift,
                project_sec,
                frame_period_ms,
                track_index,
                tracks.len(),
                progress,
            );
            let project_pitch_offset = params
                .pitch_edit
                .iter()
                .zip(params.pitch_orig.iter())
                .map(|(edited, original)| {
                    if *edited > 0.0 && *original > 0.0 { edited - original } else { 0.0 }
                })
                .collect::<Vec<_>>();
            params.extra_curves.insert(
                "mld5_project_pitch_offset".to_string(),
                project_pitch_offset,
            );
            params.extra_params.insert(
                "mld5_pitch_source".to_string(),
                if reanalyze_pitch_with_game_fcpe { 1.0 } else { 0.0 },
            );
            // In the optional path the imported contour is only a temporary
            // preview.  Once GAME/FCPE source analysis is ready, the scheduler
            // replaces both curves and reapplies the exact MPD note offsets.
            if reanalyze_pitch_with_game_fcpe {
                params.pitch_edit_user_modified = false;
            }
            for (clip_id, source_curve, target_curve, render_target_curve) in
                clip_pitch_curves
            {
                params
                    .extra_curves
                    .insert(format!("mld5_source_pitch::{clip_id}"), source_curve);
                params
                    .extra_curves
                    .insert(format!("mld5_target_pitch::{clip_id}"), target_curve);
                params.extra_curves.insert(
                    format!("mld5_render_target_pitch::{clip_id}"),
                    render_target_curve,
                );
            }
            timeline.params_by_root_track.insert(track_id.clone(), params);
        }
        if track_index == 0 {
            timeline.selected_track_id = Some(track_id);
        }
    }

    // Keep Melodyne's persisted segmentation and note-object controls
    // available to the original-source wrench editor. This avoids replacing
    // them with a new GAME pass when an imported MPD clip is selected.
    register_project_note_objects(&graph, &tracks, &resolved_sources, compose_tracks.as_ref());

    timeline.playhead_sec = 0.0;
    timeline.project_sec = project_sec;
    timeline.selected_clip_id = timeline.clips.first().map(|clip| clip.id.clone());
    if let Some(first_clip_track) = timeline.clips.first().map(|clip| clip.track_id.clone()) {
        timeline.selected_track_id = Some(first_clip_track);
    }
    progress(0.96, "finalize", tracks.len(), tracks.len());

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
    import_graph(path, graph, &|_, _, _, _| {}, None, false)
}

pub fn import_mpd_file(
    path: &Path,
    progress: &ImportProgress<'_>,
    compose_track_indices: Option<&[usize]>,
    pitch_source: Option<&str>,
) -> Result<MelodyneImportResult, String> {
    progress(0.0, "open", 0, 1);
    match decode_graph_file(path, progress).and_then(Graph::parse) {
        Ok(graph) if graph.find_first_class("MUPerformance").is_some() => {
            import_graph(
                path,
                graph,
                progress,
                compose_track_indices,
                pitch_source == Some("game_fcpe"),
            )
        }
        Ok(_) => {
            // Compatibility path for older MPD variants.  It is reached only
            // when the bounded streaming graph reader cannot identify a
            // performance graph.
            progress(0.20, "legacy_scan", 0, 1);
            let data = fs::read(path).map_err(|error| format!("failed to read MPD: {error}"))?;
            let result = import_mpd(path, &data);
            progress(0.96, "finalize", 1, 1);
            result
        }
        Err(error) => {
            let file_len = fs::metadata(path).map(|meta| meta.len()).unwrap_or(0);
            if file_len > 64 * 1024 * 1024 {
                Err(format!("large MPD graph parsing failed: {error}"))
            } else {
                progress(0.20, "legacy_scan", 0, 1);
                let data = fs::read(path)
                    .map_err(|read_error| format!("failed to read MPD: {read_error}"))?;
                let result = import_mpd(path, &data);
                progress(0.96, "finalize", 1, 1);
                result
            }
        }
    }
}

#[derive(Debug, Clone, serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct MelodyneTrackInspection {
    pub index: usize,
    pub name: String,
    pub suggested_compose: bool,
    pub element_count: usize,
}

pub fn inspect_mpd_tracks(path: &Path) -> Result<Vec<MelodyneTrackInspection>, String> {
    let graph = decode_graph_file(path, &|_, _, _, _| {}).and_then(Graph::parse)?;
    let (tracks, _) = collect_project(&graph)?;
    Ok(tracks.into_iter().enumerate().map(|(index, track)| MelodyneTrackInspection {
        index,
        name: track.name,
        suggested_compose: track.melodic,
        element_count: track.elements.len(),
    }).collect())
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
