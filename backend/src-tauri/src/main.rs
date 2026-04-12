// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

#[cfg(target_os = "linux")]
fn sanitize_gtk_modules_for_appimage() {
    // Some Linux environments inject xapp-gtk3-module globally, but the module
    // is optional and may be unavailable in AppImage runtime.
    if std::env::var_os("APPIMAGE").is_none() {
        return;
    }

    let raw = std::env::var("GTK_MODULES").unwrap_or_default();
    let filtered = raw
        .split(':')
        .map(str::trim)
        .filter(|m| !m.is_empty() && *m != "xapp-gtk3-module")
        .collect::<Vec<_>>()
        .join(":");

    std::env::set_var("GTK_MODULES", filtered);
}

fn main() {
    #[cfg(target_os = "linux")]
    sanitize_gtk_modules_for_appimage();

    backend_lib::run()
}
