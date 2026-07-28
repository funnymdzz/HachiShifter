//! Embedded Model Context Protocol bridge.
//!
//! The server binds loopback only and implements the MCP 2025-06-18
//! Streamable-HTTP request subset used by tool/resource clients.  Its generic
//! Tauri invoke and UI-eval tools deliberately expose the same operation
//! surface as the running application, while state/file tools make diagnostic
//! context available without screen scraping.

use base64::Engine;
use serde_json::{json, Value};
use std::collections::HashMap;
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::path::{Path, PathBuf};
use std::sync::{mpsc, Mutex, OnceLock};
use std::time::Duration;
use tauri::Manager;

const MAX_HTTP_BYTES: usize = 8 * 1024 * 1024;
const MAX_FILE_BYTES: usize = 4 * 1024 * 1024;

#[derive(Clone, serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct McpServerInfo {
    pub enabled: bool,
    pub endpoint: String,
    pub protocol_version: String,
    pub descriptor_path: Option<String>,
}

static SERVER_INFO: OnceLock<McpServerInfo> = OnceLock::new();
static UI_PENDING: OnceLock<Mutex<HashMap<String, mpsc::SyncSender<Result<Value, String>>>>> =
    OnceLock::new();

fn pending() -> &'static Mutex<HashMap<String, mpsc::SyncSender<Result<Value, String>>>> {
    UI_PENDING.get_or_init(|| Mutex::new(HashMap::new()))
}

pub fn server_info() -> McpServerInfo {
    SERVER_INFO.get().cloned().unwrap_or(McpServerInfo {
        enabled: false,
        endpoint: String::new(),
        protocol_version: "2025-06-18".to_string(),
        descriptor_path: None,
    })
}

pub fn complete_ui_request(request_id: String, value: Option<Value>, error: Option<String>) {
    let sender = pending()
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .remove(&request_id);
    if let Some(sender) = sender {
        let _ = sender.send(match error {
            Some(error) => Err(error),
            None => Ok(value.unwrap_or(Value::Null)),
        });
    }
}

fn eval_ui(app: &tauri::AppHandle, body: &str) -> Result<Value, String> {
    let request_id = uuid::Uuid::new_v4().simple().to_string();
    let (sender, receiver) = mpsc::sync_channel(1);
    pending()
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .insert(request_id.clone(), sender);
    let window = app
        .get_webview_window("main")
        .ok_or_else(|| "main webview is not ready".to_string())?;
    let request_id_js = serde_json::to_string(&request_id).map_err(|error| error.to_string())?;
    let body_js = serde_json::to_string(body).map_err(|error| error.to_string())?;
    let script = format!(
        r#"void (async () => {{
            const requestId = {request_id_js};
            const source = {body_js};
            const invoke = window.__TAURI_INTERNALS__?.invoke ?? window.__TAURI__?.core?.invoke;
            const complete = async (value, error) => {{
                if (!invoke) throw new Error("Tauri invoke bridge is unavailable");
                let normalized = null;
                if (value !== undefined) {{
                    try {{ normalized = JSON.parse(JSON.stringify(value)); }}
                    catch (_) {{ normalized = String(value); }}
                }}
                await invoke("mcp_complete_request", {{ requestId, value: normalized, error }});
            }};
            try {{
                const run = new Function("return (async () => {{\n" + source + "\n}})()");
                await complete(await run(), null);
            }} catch (error) {{
                await complete(null, String(error?.stack ?? error));
            }}
        }})()"#
    );
    if let Err(error) = window.eval(&script) {
        pending()
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .remove(&request_id);
        return Err(error.to_string());
    }
    receiver
        .recv_timeout(Duration::from_secs(120))
        .map_err(|_| "UI operation timed out".to_string())?
}

fn state_snapshot(app: &tauri::AppHandle, include_curves: bool) -> Result<Value, String> {
    let state = app.state::<crate::state::AppState>();
    let mut timeline = state
        .timeline
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .clone();
    if !include_curves {
        for params in timeline.params_by_root_track.values_mut() {
            let summary = json!({
                "framePeriodMs": params.frame_period_ms,
                "pitchOrigFrames": params.pitch_orig.len(),
                "pitchEditFrames": params.pitch_edit.len(),
                "extraCurveFrames": params.extra_curves.iter()
                    .map(|(name, values)| (name.clone(), values.len()))
                    .collect::<HashMap<_, _>>(),
                "extraParams": params.extra_params,
            });
            params.pitch_orig.clear();
            params.pitch_edit.clear();
            params.tension_orig.clear();
            params.tension_edit.clear();
            params.extra_curves.clear();
            params.extra_params.insert(
                "mcp_curve_summary_bytes".to_string(),
                serde_json::to_vec(&summary).map(|bytes| bytes.len()).unwrap_or(0) as f64,
            );
        }
    }
    let project = state
        .project
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner());
    let playback = state.audio_engine.snapshot_state();
    Ok(json!({
        "timeline": timeline,
        "project": {
            "name": project.name,
            "path": project.path,
            "dirty": project.dirty,
            "beatsPerBar": project.beats_per_bar,
            "gridSize": project.grid_size,
        },
        "playback": {
            "isPlaying": playback.is_playing,
            "target": playback.target,
            "baseSec": playback.base_sec,
            "positionSec": playback.position_sec,
            "durationSec": playback.duration_sec,
        },
        "timelineVersion": state.timeline_version.load(std::sync::atomic::Ordering::Acquire),
    }))
}

fn read_file(arguments: &Value) -> Result<Value, String> {
    let path = arguments
        .get("path")
        .and_then(Value::as_str)
        .ok_or_else(|| "path is required".to_string())?;
    let offset = arguments.get("offset").and_then(Value::as_u64).unwrap_or(0);
    let length = arguments
        .get("length")
        .and_then(Value::as_u64)
        .unwrap_or(256 * 1024)
        .min(MAX_FILE_BYTES as u64) as usize;
    let mut file = std::fs::File::open(path).map_err(|error| error.to_string())?;
    use std::io::{Seek, SeekFrom};
    file.seek(SeekFrom::Start(offset))
        .map_err(|error| error.to_string())?;
    let mut bytes = Vec::with_capacity(length);
    file.take(length as u64)
        .read_to_end(&mut bytes)
        .map_err(|error| error.to_string())?;
    let encoding = arguments
        .get("encoding")
        .and_then(Value::as_str)
        .unwrap_or("utf8");
    let data = if encoding.eq_ignore_ascii_case("base64") {
        base64::engine::general_purpose::STANDARD.encode(&bytes)
    } else {
        String::from_utf8_lossy(&bytes).to_string()
    };
    Ok(json!({"path": path, "offset": offset, "bytesRead": bytes.len(), "encoding": encoding, "data": data}))
}

fn list_directory(arguments: &Value) -> Result<Value, String> {
    let path = arguments
        .get("path")
        .and_then(Value::as_str)
        .ok_or_else(|| "path is required".to_string())?;
    let mut entries = std::fs::read_dir(path)
        .map_err(|error| error.to_string())?
        .filter_map(Result::ok)
        .map(|entry| {
            let metadata = entry.metadata().ok();
            json!({
                "name": entry.file_name().to_string_lossy(),
                "path": entry.path().to_string_lossy(),
                "isDirectory": metadata.as_ref().is_some_and(|value| value.is_dir()),
                "size": metadata.as_ref().map(|value| value.len()),
            })
        })
        .collect::<Vec<_>>();
    entries.sort_by(|left, right| left["name"].as_str().cmp(&right["name"].as_str()));
    Ok(json!({"path": path, "entries": entries}))
}

fn tools() -> Value {
    json!([
        {
            "name": "hachi_get_state",
            "description": "Read the running HachiShifter project, timeline, tracks, clips, playback state and optionally every automation/F0 curve.",
            "inputSchema": {"type":"object","properties":{"includeCurves":{"type":"boolean","default":false}}}
        },
        {
            "name": "hachi_invoke",
            "description": "Invoke any registered HachiShifter Tauri command in the running application and return its complete result.",
            "inputSchema": {"type":"object","properties":{"command":{"type":"string"},"arguments":{"type":"object"}},"required":["command"]}
        },
        {
            "name": "hachi_ui_eval",
            "description": "Execute asynchronous JavaScript in the HachiShifter webview. It can inspect DOM/UI state, click controls, dispatch events and return JSON-serializable diagnostics.",
            "inputSchema": {"type":"object","properties":{"code":{"type":"string"}},"required":["code"]}
        },
        {
            "name": "hachi_read_file",
            "description": "Read any diagnostic, project, audio sidecar or application file in bounded chunks as UTF-8 or base64.",
            "inputSchema": {"type":"object","properties":{"path":{"type":"string"},"offset":{"type":"integer","minimum":0},"length":{"type":"integer","minimum":1,"maximum":4194304},"encoding":{"type":"string","enum":["utf8","base64"]}},"required":["path"]}
        },
        {
            "name": "hachi_list_directory",
            "description": "List directory contents with paths, types and sizes for debugging project media and application data.",
            "inputSchema": {"type":"object","properties":{"path":{"type":"string"}},"required":["path"]}
        }
    ])
}

fn call_tool(app: &tauri::AppHandle, name: &str, arguments: &Value) -> Result<Value, String> {
    match name {
        "hachi_get_state" => state_snapshot(
            app,
            arguments
                .get("includeCurves")
                .and_then(Value::as_bool)
                .unwrap_or(false),
        ),
        "hachi_invoke" => {
            let command = arguments
                .get("command")
                .and_then(Value::as_str)
                .ok_or_else(|| "command is required".to_string())?;
            let invoke_arguments = arguments.get("arguments").cloned().unwrap_or_else(|| json!({}));
            let command_js = serde_json::to_string(command).map_err(|error| error.to_string())?;
            let args_js = serde_json::to_string(&invoke_arguments).map_err(|error| error.to_string())?;
            eval_ui(app, &format!(
                "const invoke = window.__TAURI_INTERNALS__?.invoke ?? window.__TAURI__?.core?.invoke;\nreturn await invoke({command_js}, {args_js});"
            ))
        }
        "hachi_ui_eval" => eval_ui(
            app,
            arguments
                .get("code")
                .and_then(Value::as_str)
                .ok_or_else(|| "code is required".to_string())?,
        ),
        "hachi_read_file" => read_file(arguments),
        "hachi_list_directory" => list_directory(arguments),
        _ => Err(format!("unknown tool: {name}")),
    }
}

fn tool_result(result: Result<Value, String>) -> Value {
    match result {
        Ok(value) => json!({
            "content": [{"type":"text", "text": serde_json::to_string(&value).unwrap_or_else(|_| "null".to_string())}],
            "structuredContent": value,
            "isError": false,
        }),
        Err(error) => json!({"content":[{"type":"text","text":error}],"isError":true}),
    }
}

fn rpc(app: &tauri::AppHandle, request: Value) -> Option<Value> {
    let id = request.get("id").cloned();
    let method = request.get("method").and_then(Value::as_str).unwrap_or("");
    if id.is_none() {
        return None;
    }
    let result = match method {
        "initialize" => json!({
            "protocolVersion":"2025-06-18",
            "capabilities":{"tools":{},"resources":{}},
            "serverInfo":{"name":"HachiShifter","version":env!("CARGO_PKG_VERSION")}
        }),
        "ping" => json!({}),
        "tools/list" => json!({"tools":tools()}),
        "tools/call" => {
            let params = request.get("params").cloned().unwrap_or_else(|| json!({}));
            let name = params.get("name").and_then(Value::as_str).unwrap_or("");
            let arguments = params.get("arguments").cloned().unwrap_or_else(|| json!({}));
            tool_result(call_tool(app, name, &arguments))
        }
        "resources/list" => json!({"resources":[
            {"uri":"hachishifter://state/current","name":"Current HachiShifter state","mimeType":"application/json"},
            {"uri":"hachishifter://mcp/info","name":"HachiShifter MCP connection","mimeType":"application/json"}
        ]}),
        "resources/read" => {
            let uri = request.pointer("/params/uri").and_then(Value::as_str).unwrap_or("");
            let value = match uri {
                "hachishifter://state/current" => state_snapshot(app, true),
                "hachishifter://mcp/info" => serde_json::to_value(server_info()).map_err(|error| error.to_string()),
                _ if uri.starts_with("file://") => read_file(&json!({"path": &uri[7..], "encoding":"utf8"})),
                _ => Err(format!("unknown resource: {uri}")),
            };
            match value {
                Ok(value) => json!({"contents":[{"uri":uri,"mimeType":"application/json","text":serde_json::to_string(&value).unwrap_or_default()}]}),
                Err(error) => return Some(json!({"jsonrpc":"2.0","id":id,"error":{"code":-32002,"message":error}})),
            }
        }
        _ => return Some(json!({"jsonrpc":"2.0","id":id,"error":{"code":-32601,"message":"Method not found"}})),
    };
    Some(json!({"jsonrpc":"2.0","id":id,"result":result}))
}

fn http_response(stream: &mut TcpStream, status: &str, body: &[u8], content_type: &str) {
    let header = format!(
        "HTTP/1.1 {status}\r\nContent-Type: {content_type}\r\nContent-Length: {}\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: Content-Type, Authorization, MCP-Protocol-Version, Mcp-Session-Id, Mcp-Method, Mcp-Name\r\nConnection: close\r\n\r\n",
        body.len()
    );
    let _ = stream.write_all(header.as_bytes());
    let _ = stream.write_all(body);
}

fn handle_connection(app: tauri::AppHandle, mut stream: TcpStream) {
    let _ = stream.set_read_timeout(Some(Duration::from_secs(10)));
    let mut bytes = Vec::with_capacity(4096);
    let mut header_end = None;
    while bytes.len() < MAX_HTTP_BYTES {
        let mut chunk = [0u8; 4096];
        let Ok(read) = stream.read(&mut chunk) else { return; };
        if read == 0 { break; }
        bytes.extend_from_slice(&chunk[..read]);
        if header_end.is_none() {
            header_end = bytes.windows(4).position(|window| window == b"\r\n\r\n").map(|index| index + 4);
        }
        if let Some(end) = header_end {
            let headers = String::from_utf8_lossy(&bytes[..end]);
            let content_length = headers
                .lines()
                .find_map(|line| line.split_once(':'))
                .filter(|(name, _)| name.eq_ignore_ascii_case("content-length"))
                .and_then(|(_, value)| value.trim().parse::<usize>().ok())
                .unwrap_or(0);
            if bytes.len() >= end.saturating_add(content_length) { break; }
        }
    }
    let Some(end) = header_end else {
        http_response(&mut stream, "400 Bad Request", b"missing HTTP headers", "text/plain");
        return;
    };
    let headers = String::from_utf8_lossy(&bytes[..end]);
    let first = headers.lines().next().unwrap_or("");
    if first.starts_with("OPTIONS ") {
        http_response(&mut stream, "204 No Content", &[], "text/plain");
        return;
    }
    if !first.starts_with("POST /mcp ") {
        http_response(&mut stream, "405 Method Not Allowed", b"POST /mcp", "text/plain");
        return;
    }
    let request: Value = match serde_json::from_slice(&bytes[end..]) {
        Ok(request) => request,
        Err(error) => {
            let body = json!({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":error.to_string()}}).to_string();
            http_response(&mut stream, "400 Bad Request", body.as_bytes(), "application/json");
            return;
        }
    };
    match rpc(&app, request) {
        Some(response) => http_response(
            &mut stream,
            "200 OK",
            response.to_string().as_bytes(),
            "application/json",
        ),
        None => http_response(&mut stream, "202 Accepted", &[], "application/json"),
    }
}

pub fn start(app: tauri::AppHandle, config_dir: Option<PathBuf>) -> Result<McpServerInfo, String> {
    if let Some(info) = SERVER_INFO.get() {
        return Ok(info.clone());
    }
    let requested_port = std::env::var("HACHISHIFTER_MCP_PORT")
        .ok()
        .and_then(|value| value.parse::<u16>().ok())
        .unwrap_or(18_765);
    let listener = TcpListener::bind(("127.0.0.1", requested_port))
        .or_else(|_| TcpListener::bind(("127.0.0.1", 0)))
        .map_err(|error| format!("failed to bind MCP loopback server: {error}"))?;
    let address = listener.local_addr().map_err(|error| error.to_string())?;
    let endpoint = format!("http://127.0.0.1:{}/mcp", address.port());
    let descriptor_path = config_dir.map(|dir| dir.join("mcp-connection.json"));
    let info = McpServerInfo {
        enabled: true,
        endpoint,
        protocol_version: "2025-06-18".to_string(),
        descriptor_path: descriptor_path
            .as_ref()
            .map(|path| path.to_string_lossy().to_string()),
    };
    if let Some(path) = descriptor_path.as_ref() {
        if let Some(parent) = path.parent() {
            let _ = std::fs::create_dir_all(parent);
        }
        let _ = std::fs::write(
            path,
            serde_json::to_vec_pretty(&info).unwrap_or_default(),
        );
    }
    let _ = SERVER_INFO.set(info.clone());
    std::thread::Builder::new()
        .name("hachishifter-mcp".to_string())
        .spawn(move || {
            for stream in listener.incoming() {
                let Ok(stream) = stream else { continue; };
                let app = app.clone();
                let _ = std::thread::Builder::new()
                    .name("hachishifter-mcp-client".to_string())
                    .spawn(move || handle_connection(app, stream));
            }
        })
        .map_err(|error| error.to_string())?;
    Ok(info)
}
