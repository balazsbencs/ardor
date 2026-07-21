use std::{
    io::{Read, Write},
    net::{TcpListener, TcpStream},
    sync::atomic::{AtomicU64, Ordering},
    thread,
    time::{Duration, Instant},
};

use tauri::{Emitter, Manager};
use tauri_plugin_opener::OpenerExt;

const TONE3000_CALLBACK_ADDRESS: &str = "127.0.0.1:43821";
const TONE3000_CALLBACK_PATH: &str = "/tone3000/callback";
const TONE3000_REDIRECT_URI: &str = "http://localhost:43821/tone3000/callback";

#[derive(Default)]
struct Tone3000State {
    generation: AtomicU64,
}

fn is_tone3000_authorize_url(url: &tauri::Url) -> bool {
    let has_expected_redirect = url
        .query_pairs()
        .any(|(key, value)| key == "redirect_uri" && value == TONE3000_REDIRECT_URI);
    url.scheme() == "https"
        && url.host_str() == Some("www.tone3000.com")
        && url.path() == "/api/v1/oauth/authorize"
        && has_expected_redirect
}

fn callback_url_from_request(request: &str) -> Option<String> {
    let request_line = request.lines().next()?;
    let mut parts = request_line.split_whitespace();
    if parts.next()? != "GET" {
        return None;
    }
    let target = parts.next()?;
    if target != TONE3000_CALLBACK_PATH
        && !target.starts_with(&format!("{TONE3000_CALLBACK_PATH}?"))
    {
        return None;
    }
    Some(format!("http://localhost:43821{target}"))
}

fn callback_has_state(callback_url: &str, expected_state: &str) -> bool {
    tauri::Url::parse(callback_url).ok().is_some_and(|url| {
        url.query_pairs()
            .any(|(key, value)| key == "state" && value == expected_state)
    })
}

fn write_callback_response(stream: &mut TcpStream, success: bool) -> std::io::Result<()> {
    let (status, body) = if success {
        (
            "200 OK",
            "<!doctype html><meta charset=\"utf-8\"><title>Return to Ardor</title><style>body{font:16px system-ui;display:grid;place-items:center;min-height:100vh;margin:0;background:#101512;color:#e9eee9}main{max-width:34rem;padding:2rem;text-align:center}h1{color:#c9ff3d}</style><main><h1>Return to Ardor Manager</h1><p>Your TONE3000 selection was received. You can close this tab.</p></main>",
        )
    } else {
        (
            "404 Not Found",
            "<!doctype html><meta charset=\"utf-8\"><title>Not found</title><p>Not found.</p>",
        )
    };
    let response = format!(
        "HTTP/1.1 {status}\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: {}\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n{body}",
        body.len()
    );
    stream.write_all(response.as_bytes())
}

fn handle_callback_connection(
    mut stream: TcpStream,
    app: &tauri::AppHandle,
    expected_state: &str,
) -> std::io::Result<bool> {
    stream.set_nonblocking(false)?;
    stream.set_read_timeout(Some(Duration::from_secs(3)))?;
    let mut request_bytes = Vec::with_capacity(2048);
    let mut chunk = [0_u8; 2048];
    while request_bytes.len() < 16 * 1024 {
        let read = stream.read(&mut chunk)?;
        if read == 0 {
            break;
        }
        request_bytes.extend_from_slice(&chunk[..read]);
        if request_bytes.windows(4).any(|window| window == b"\r\n\r\n") {
            break;
        }
    }
    let request = String::from_utf8_lossy(&request_bytes);
    let Some(callback_url) = callback_url_from_request(&request) else {
        write_callback_response(&mut stream, false)?;
        return Ok(false);
    };
    if !callback_has_state(&callback_url, expected_state) {
        write_callback_response(&mut stream, false)?;
        return Ok(false);
    }

    if let Some(window) = app.get_webview_window("main") {
        let _ = window.unminimize();
        let _ = window.show();
        let _ = window.set_focus();
    }
    let emitted = app
        .emit_to("main", "tone3000-oauth-callback", callback_url)
        .is_ok();
    write_callback_response(&mut stream, emitted)?;
    Ok(emitted)
}

fn listen_for_tone3000_callback(
    listener: TcpListener,
    app: tauri::AppHandle,
    generation: u64,
    expected_state: String,
) {
    let _ = listener.set_nonblocking(true);
    let deadline = Instant::now() + Duration::from_secs(10 * 60);
    while Instant::now() < deadline
        && app
            .state::<Tone3000State>()
            .generation
            .load(Ordering::SeqCst)
            == generation
    {
        match listener.accept() {
            Ok((stream, _)) => {
                if handle_callback_connection(stream, &app, &expected_state).unwrap_or(false) {
                    break;
                }
            }
            Err(error) if error.kind() == std::io::ErrorKind::WouldBlock => {
                thread::sleep(Duration::from_millis(50));
            }
            Err(_) => break,
        }
    }
}

#[tauri::command]
fn open_tone3000(
    app: tauri::AppHandle,
    state: tauri::State<'_, Tone3000State>,
    url: String,
) -> Result<(), String> {
    let authorize_url = tauri::Url::parse(&url).map_err(|error| error.to_string())?;
    if !is_tone3000_authorize_url(&authorize_url) {
        return Err("refusing to open an unexpected Tone3000 URL".into());
    }
    let expected_state = authorize_url
        .query_pairs()
        .find_map(|(key, value)| (key == "state" && !value.is_empty()).then(|| value.into_owned()))
        .ok_or_else(|| "Tone3000 authorization URL is missing state".to_string())?;

    let generation = state.generation.fetch_add(1, Ordering::SeqCst) + 1;
    let bind_deadline = Instant::now() + Duration::from_secs(1);
    let listener = loop {
        match TcpListener::bind(TONE3000_CALLBACK_ADDRESS) {
            Ok(listener) => break listener,
            Err(_) if Instant::now() < bind_deadline => {
                thread::sleep(Duration::from_millis(50));
            }
            Err(_) => {
                return Err("Tone3000 callback port 43821 is unavailable".into());
            }
        }
    };
    app.opener()
        .open_url(authorize_url.as_str(), None::<&str>)
        .map_err(|error| error.to_string())?;
    let callback_app = app.clone();
    thread::spawn(move || {
        listen_for_tone3000_callback(listener, callback_app, generation, expected_state)
    });
    Ok(())
}

#[tauri::command]
fn cancel_tone3000(state: tauri::State<'_, Tone3000State>) {
    state.generation.fetch_add(1, Ordering::SeqCst);
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .manage(Tone3000State::default())
        .invoke_handler(tauri::generate_handler![open_tone3000, cancel_tone3000])
        .run(tauri::generate_context!())
        .expect("error while running Ardor Manager");
}

#[cfg(test)]
mod tests {
    use super::{callback_has_state, callback_url_from_request, is_tone3000_authorize_url};

    #[test]
    fn only_allows_the_tone3000_authorize_endpoint_and_loopback_callback() {
        let allowed = tauri::Url::parse(
            "https://www.tone3000.com/api/v1/oauth/authorize?client_id=t3k_pub_test&redirect_uri=http%3A%2F%2Flocalhost%3A43821%2Ftone3000%2Fcallback",
        )
        .unwrap();
        let wrong_host = tauri::Url::parse(
            "https://www.tone3000.com.evil.example/api/v1/oauth/authorize?redirect_uri=http%3A%2F%2Flocalhost%3A43821%2Ftone3000%2Fcallback",
        )
        .unwrap();
        let wrong_callback = tauri::Url::parse(
            "https://www.tone3000.com/api/v1/oauth/authorize?redirect_uri=https%3A%2F%2Fevil.example%2Fcallback",
        )
        .unwrap();

        assert!(is_tone3000_authorize_url(&allowed));
        assert!(!is_tone3000_authorize_url(&wrong_host));
        assert!(!is_tone3000_authorize_url(&wrong_callback));
    }

    #[test]
    fn accepts_only_the_expected_callback_request_path() {
        assert_eq!(
            callback_url_from_request(
                "GET /tone3000/callback?code=abc&state=xyz HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
            ),
            Some("http://localhost:43821/tone3000/callback?code=abc&state=xyz".into())
        );
        assert_eq!(
            callback_url_from_request("GET /other?code=abc HTTP/1.1\r\n\r\n"),
            None
        );
        assert_eq!(
            callback_url_from_request("POST /tone3000/callback?code=abc HTTP/1.1\r\n\r\n"),
            None
        );
        assert!(callback_has_state(
            "http://localhost:43821/tone3000/callback?code=abc&state=xyz",
            "xyz"
        ));
        assert!(!callback_has_state(
            "http://localhost:43821/tone3000/callback?code=abc&state=wrong",
            "xyz"
        ));
    }
}
