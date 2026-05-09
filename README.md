# SonioxPP

A C++17 library for the [Soniox](https://soniox.com) API — real-time and async speech-to-text, plus text-to-speech.

---

## Features

| | Real-time WebSocket | Async REST |
| --- | :---: | :---: |
| **STT** | ✓ | ✓ |
| **TTS** | ✓ | ✓ |
| Speaker diarization | ✓ | ✓ |
| Language identification | ✓ | ✓ |
| Endpoint detection | ✓ | |
| Translation | ✓ | ✓ |
| Custom vocabulary | ✓ | ✓ |
| Webhook on completion | | ✓ |
| Temporary API keys | | ✓ |

---

## Requirements

| Dependency | Fetched automatically? | Notes |
| --- | :---: | --- |
| C++17 compiler | — | GCC 9+, Clang 10+, MSVC 2019+ |
| CMake 3.16+ | — | |
| libcurl 8.11 | ✓ | Built from source via FetchContent |
| OpenSSL | **No** | Linux/macOS only — install via system package manager |
| libwebsockets 4.3 | ✓ | WebSocket transport on Linux/macOS |
| nlohmann/json 3.12 | ✓ | |
| spdlog 1.15 | ✓ | |
| miniaudio 0.11 | ✓ | Microphone example only |
| cppcodec 0.2 | ✓ | Base64 codec — TTS examples only |
| CLI11 2.4 | ✓ | Argument parsing — examples only |

On **Windows** all TLS and WebSocket I/O uses the built-in Schannel and WinHTTP APIs — no OpenSSL required.

On **Linux/macOS** install OpenSSL before building:

```bash
# macOS
brew install openssl

# Ubuntu/Debian
sudo apt install libssl-dev
```

---

## Build

```bash
git clone https://github.com/fatehmtd/SonioxPP.git
cd SonioxPP
cmake -B build -DSONIOX_BUILD_EXAMPLES=ON
cmake --build build
```

To build the library only:

```bash
cmake -B build -DSONIOX_BUILD_EXAMPLES=OFF
cmake --build build
```

---

## Integrating into your project

### FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    sonioxpp
    GIT_REPOSITORY https://github.com/fatehmtd/SonioxPP.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(sonioxpp)

target_link_libraries(your_target PRIVATE sonioxpp)
```

### Git submodule

```bash
git submodule add https://github.com/fatehmtd/SonioxPP.git third_party/SonioxPP
```

```cmake
add_subdirectory(third_party/SonioxPP/lib/sonioxpp)
target_link_libraries(your_target PRIVATE sonioxpp)
```

---

## Quick start

```bash
export SONIOX_API_KEY=your_key_here   # or set SONIOX_API_KEY on Windows
```

All public types live in `namespace soniox`. Include the single aggregator header:

```cpp
#include <sonioxpp/soniox.hpp>
```

### STT — real-time streaming

```cpp
soniox::RealtimeConfig cfg;
cfg.api_key        = std::getenv("SONIOX_API_KEY");
cfg.language_hints = {"en"};
cfg.enable_speaker_diarization = true;

soniox::RealtimeClient client;
client.setOnTokens([](const std::vector<soniox::Token>& tokens, bool has_final) {
    for (auto& t : tokens) std::cout << t.text;
});
client.setOnFinished([] { std::cout << "\nDone.\n"; });
client.setOnError([](const soniox::RealtimeError& err) {
    std::cerr << "Error " << err.error_code << ": " << err.error_message << "\n";
});

client.connect(cfg);

std::thread sender([&] {
    std::ifstream f("audio.wav", std::ios::binary);
    std::vector<uint8_t> buf(4096);
    while (f.read(reinterpret_cast<char*>(buf.data()), buf.size()) || f.gcount())
        client.sendAudio(buf.data(), static_cast<size_t>(f.gcount()));
    client.sendEndOfAudio();
});

client.run();   // blocks until the session finishes
sender.join();
```

### STT — async (one call)

```cpp
soniox::AsyncConfig cfg;
cfg.api_key                    = std::getenv("SONIOX_API_KEY");
cfg.language_hints             = {"en"};
cfg.enable_speaker_diarization = true;

soniox::AsyncClient client(cfg.api_key);
auto tokens = client.transcribeFile("audio.wav", cfg);

for (auto& tok : tokens) std::cout << tok.text;
std::cout << '\n';
```

### STT — async step-by-step

```cpp
soniox::AsyncClient client(std::getenv("SONIOX_API_KEY"));

std::string file_id = client.uploadFile("audio.wav");

soniox::AsyncConfig cfg;
cfg.file_id = file_id;
std::string tx_id = client.createTranscription(cfg);

soniox::AsyncTranscription tx;
do {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    tx = client.getTranscription(tx_id);
} while (tx.status == soniox::TranscriptionStatus::Pending ||
         tx.status == soniox::TranscriptionStatus::Running);

auto tokens = client.getTranscript(tx_id);
client.deleteTranscription(tx_id);
client.deleteFile(file_id);
```

### TTS — REST

```cpp
soniox::TtsRestClient tts(std::getenv("SONIOX_API_KEY"));

soniox::TtsGenerateRequest req;
req.language     = "en";
req.voice        = "Adrian";
req.audio_format = soniox::tts::audio_formats::wav;
req.text         = "Hello from Soniox.";

auto resp = tts.generateSpeech(req);
std::ofstream out("speech.wav", std::ios::binary);
out.write(reinterpret_cast<const char*>(resp.body.data()),
          static_cast<std::streamsize>(resp.body.size()));
```

### TTS — real-time streaming

```cpp
soniox::TtsRealtimeClient tts;

tts.setOnParsedMessage([](const soniox::TtsRealtimeMessage& m) {
    if (!m.audio.empty()) { /* base64-decode m.audio and write PCM */ }
    if (m.terminated)     { /* stream done */ }
    if (m.error_code)     { std::cerr << m.error_message << '\n'; }
});

tts.connect();

soniox::TtsRealtimeStreamConfig cfg;
cfg.api_key      = std::getenv("SONIOX_API_KEY");
cfg.stream_id    = "stream-1";
cfg.language     = "en";
cfg.voice        = "Adrian";
cfg.audio_format = soniox::tts::audio_formats::wav;

tts.startStream(cfg);
tts.sendText(cfg.stream_id, "Hello from real-time TTS.", /*text_end=*/true);
// wait for terminated callback, then:
tts.close();
```

### Typed STT REST client

```cpp
soniox::SttRestClient stt(std::getenv("SONIOX_API_KEY"));

// Upload
soniox::SttFile file = stt.uploadFileTyped("audio.wav");

// Transcribe
soniox::SttTranscription tx = stt.createTranscriptionTyped({
    .model          = soniox::stt::models::async_v4,
    .file_id        = file.id,
    .language_hints = {"en"},
});

// Poll, retrieve, clean up
// ...
stt.deleteFile(file.id);
```

### Temporary API keys

```cpp
soniox::AuthClient auth(std::getenv("SONIOX_API_KEY"));

auto tmp = auth.createTemporaryApiKeyTyped({
    .usage_type         = soniox::auth::temporary_api_key_usage::transcribe_websocket,
    .expires_in_seconds = 900,
    .single_use         = true,
});
// hand tmp.api_key to a client-side WebSocket session
```

### Error handling

```cpp
try {
    soniox::SttRestClient stt(std::getenv("SONIOX_API_KEY"));
    stt.getFileTyped("bad-id");
} catch (const soniox::SonioxApiException& ex) {
    const auto& d = ex.detail();
    std::cerr << "status=" << d.status_code
              << " type="  << d.error_type
              << " msg="   << d.message
              << " req="   << d.request_id << '\n';
}
```

---

## API reference

### Constants

```cpp
namespace soniox::stt::models         // realtime_v4, realtime_v3, async_v4, async_v3
namespace soniox::stt::audio_formats  // auto_detect, pcm_s16le, wav, mp3, ogg, flac
namespace soniox::stt::translation_types  // one_way, two_way
namespace soniox::tts::models         // realtime_v1
namespace soniox::tts::audio_formats  // wav, mp3, opus, flac, pcm_s16le, pcm_s16be
namespace soniox::auth::temporary_api_key_usage  // transcribe_websocket, tts_rt
```

---

### `soniox::RealtimeConfig`

| Field | Type | Default | Description |
| --- | --- | --- | --- |
| `api_key` | `string` | — | **Required** |
| `model` | `string` | `stt-rt-v4` | Model ID |
| `audio_format` | `string` | `auto` | Format hint — `auto`, `pcm_s16le`, `wav`, `mp3`, etc. |
| `sample_rate` | `int` | `0` | Hz — required when `audio_format` is a raw PCM format |
| `num_channels` | `int` | `0` | Channels — required when `audio_format` is a raw PCM format |
| `language_hints` | `vector<string>` | `{}` | BCP-47 codes (up to 100) |
| `language_hints_strict` | `bool` | `false` | Disable fallback to other languages |
| `enable_language_identification` | `bool` | `false` | Tag each token with a language |
| `enable_speaker_diarization` | `bool` | `false` | Assign speaker IDs |
| `enable_endpoint_detection` | `bool` | `false` | Detect speech pauses (emits `<end>` token) |
| `max_endpoint_delay_ms` | `int` | `0` | Endpointing delay 500–3000 ms; 0 = server default (2000) |
| `context` | `Context` | | Domain hints and custom vocabulary |
| `translation` | `Translation` | | Translation settings |

### `soniox::RealtimeClient`

| Method | Description |
| --- | --- |
| `setOnTokens(cb)` | Token batch callback — `(tokens, has_final)` |
| `setOnFinished(cb)` | Called once when server signals `finished: true` |
| `setOnError(cb)` | Network or protocol error |
| `connect(config)` | Handshake and send initial config |
| `sendAudio(data, size)` | Raw audio bytes |
| `sendAudio(vector)` | Vector overload |
| `sendEndOfAudio()` | Signal end-of-stream (empty binary frame) |
| `run()` | Block until session ends |
| `close()` | Force-close the connection |

---

### `soniox::AsyncConfig`

| Field | Type | Default | Description |
| --- | --- | --- | --- |
| `api_key` | `string` | — | **Required** |
| `model` | `string` | `stt-async-v4` | Model ID |
| `language_hints` | `vector<string>` | `{}` | BCP-47 codes |
| `language_hints_strict` | `bool` | `false` | Restrict to hinted languages only |
| `enable_language_identification` | `bool` | `false` | |
| `enable_speaker_diarization` | `bool` | `false` | |
| `context` | `Context` | | Domain hints and custom vocabulary |
| `translation` | `Translation` | | |
| `audio_url` | `string` | | Public HTTPS URL (alternative to upload) |
| `file_id` | `string` | | File ID returned by `AsyncClient::uploadFile()` |

### `soniox::AsyncClient`

| Method | Description |
| --- | --- |
| `uploadFile(path)` | Upload local audio; returns file ID |
| `deleteFile(id)` | Delete an uploaded file |
| `createTranscription(cfg)` | Start a job; returns job ID |
| `getTranscription(id)` | Poll job status → `AsyncTranscription` |
| `getTranscript(id)` | Retrieve completed tokens |
| `deleteTranscription(id)` | Delete a finished job |
| `transcribeFile(path, cfg)` | Full lifecycle in one call |

---

### `soniox::SttRestClient`

Full typed wrappers around the Soniox STT REST API. Each operation has a raw-JSON overload (returns `std::string`) and a `Typed` overload (returns a struct).

| Method | Returns |
| --- | --- |
| `uploadFileTyped(path)` | `SttFile` |
| `getFileTyped(id)` | `SttFile` |
| `getFileUrlTyped(id)` | `SttFileUrl` |
| `getFilesCountTyped()` | `SttFilesCount` |
| `listFilesTyped(query)` | `SttListFilesTypedResult` |
| `deleteFile(id)` | `void` |
| `createTranscriptionTyped(req)` | `SttTranscription` |
| `getTranscriptionTyped(id)` | `SttTranscription` |
| `getTranscriptionTranscriptTyped(id)` | `SttTranscript` |
| `getTranscriptionsCountTyped()` | `SttTranscriptionsCount` |
| `listTranscriptionsTyped(query)` | `SttListTranscriptionsTypedResult` |
| `deleteTranscription(id)` | `void` |
| `getModels()` | `string` (raw JSON) |

### `soniox::SttCreateTranscriptionRequest`

| Field | Type | Default | Description |
| --- | --- | --- | --- |
| `model` | `string` | `stt-async-v4` | Model ID |
| `file_id` | `string` | | Source file UUID — set one of `file_id` or `audio_url` |
| `audio_url` | `string` | | Public HTTPS URL to audio — set one of `file_id` or `audio_url` |
| `language_hints` | `vector<string>` | `{}` | BCP-47 codes (up to 100) |
| `language_hints_strict` | `bool` | `false` | Restrict to hinted languages only |
| `enable_speaker_diarization` | `bool` | `false` | |
| `enable_language_identification` | `bool` | `false` | |
| `context_json` | `string` | | Serialised context JSON |
| `translation_json` | `string` | | Serialised translation JSON |
| `webhook_url` | `string` | | HTTPS URL for completion callback (max 256 chars) |
| `webhook_auth_header_name` | `string` | | Auth header name for webhook delivery |
| `webhook_auth_header_value` | `string` | | Auth header value (masked in API responses) |
| `client_reference_id` | `string` | | Opaque tracking tag (max 256 chars) |

---

### `soniox::SttRealtimeClient`

Low-level typed WebSocket client. Delivers raw JSON message strings; parsing is up to the caller.

| Method | Description |
| --- | --- |
| `setOnMessage(cb)` | Raw JSON message callback |
| `setOnError(cb)` | Error callback |
| `setOnClosed(cb)` | Connection closed callback |
| `connect(config)` | Connect and send initial config |
| `sendAudio(chunk)` | Send audio chunk |
| `sendEndOfAudio()` | Signal end-of-stream |
| `sendManualFinalize()` | Request immediate finalization (`{"type":"finalize"}`) |
| `sendKeepalive()` | Send keepalive (`{"type":"keepalive"}`) |
| `close()` | Close the connection |

---

### `soniox::TtsRestClient`

| Method | Returns |
| --- | --- |
| `generateSpeech(req)` | `HttpResponse` — `body` contains raw audio bytes |
| `getModelsTyped()` | `TtsModelsResponse` |

### `soniox::TtsGenerateRequest`

| Field | Default | Description |
| --- | --- | --- |
| `model` | `tts-rt-v1` | Model ID |
| `language` | | BCP-47 code (required, e.g. `"en"`) |
| `voice` | | Voice ID (required, e.g. `"Adrian"`) |
| `audio_format` | | `wav`, `mp3`, `opus`, `flac`, `pcm_s16le`, `pcm_s16be` (required) |
| `text` | | Text to synthesise (max 5 000 chars) |
| `sample_rate` | `0` | Output sample rate Hz; 0 = format default (typically 24 000 Hz) |
| `bitrate` | `0` | Output bitrate bps for lossy formats; 0 = format default |

**Available voices:** `Maya`, `Daniel`, `Noah`, `Nina`, `Emma`, `Jack`, `Adrian`, `Claire`, `Grace`, `Owen`, `Mina`, `Kenji`

---

### `soniox::TtsRealtimeClient`

| Method | Description |
| --- | --- |
| `setOnParsedMessage(cb)` | Parsed `TtsRealtimeMessage` callback |
| `setOnMessage(cb)` | Raw JSON message callback |
| `setOnError(cb)` | Error callback |
| `setOnClosed(cb)` | Connection closed callback |
| `connect()` | Open WebSocket (no auth at this stage) |
| `startStream(config)` | Send stream config with API key |
| `sendText(stream_id, text, text_end)` | Push text chunk; `text_end=true` signals end |
| `cancelStream(stream_id)` | Abort the stream |
| `sendKeepalive()` | Send connection-level keepalive (`{"keep_alive":true}`) |
| `close()` | Close the connection |

### `soniox::TtsRealtimeMessage`

| Field | Description |
| --- | --- |
| `stream_id` | Stream this message belongs to |
| `audio` | Base64-encoded audio chunk; empty for non-audio messages |
| `terminated` | `true` when the stream is complete; `stream_id` may then be reused |
| `error_code` | Non-zero on stream error (connection stays open) |
| `error_message` | Error description |

### `soniox::TtsRealtimeStreamConfig`

| Field | Default | Description |
| --- | --- | --- |
| `api_key` | | Soniox API key or temporary key (required) |
| `stream_id` | | Unique per active stream; reusable after `terminated` (required) |
| `model` | `tts-rt-v1` | Model ID |
| `language` | | BCP-47 code (required) |
| `voice` | | Voice ID (required) |
| `audio_format` | | Output format (required) |
| `sample_rate` | `0` | Hz; 0 = format default |
| `bitrate` | `0` | bps for lossy formats; 0 = format default |

---

### `soniox::AuthClient`

| Method | Returns |
| --- | --- |
| `createTemporaryApiKeyTyped(req)` | `TemporaryApiKeyResponse` — `api_key`, `expires_at` |

### `soniox::TemporaryApiKeyRequest`

| Field | Type | Description |
| --- | --- | --- |
| `usage_type` | `string` | `"transcribe_websocket"` or `"tts_rt"` |
| `expires_in_seconds` | `int` | Key TTL (max 3 600) |
| `single_use` | `bool` | Invalidate after first use |
| `max_session_duration_seconds` | `int` | Cap session length; 0 = no extra cap |
| `client_reference_id` | `string` | Opaque tracking tag (max 256 chars) |

---

### `soniox::Token`

| Field | Type | Description |
| --- | --- | --- |
| `text` | `string` | Token text; `"<fin>"` after `sendManualFinalize`, `"<end>"` at endpoints |
| `is_final` | `bool` | Will not be revised when `true` |
| `speaker` | `int` | Speaker ID (0-based); populated with diarization |
| `language` | `string` | BCP-47 detected language; populated with lang-ID |
| `translation_status` | `string` | `"original"`, `"translation"`, or `"none"` |
| `start_ms` | `int` | Start time in ms (async API only) |
| `duration_ms` | `int` | Duration in ms (async API only) |

### `soniox::SonioxApiException`

Thrown on HTTP ≥ 400 REST responses.

```cpp
catch (const soniox::SonioxApiException& ex) {
    ex.detail().status_code;       // int  (e.g. 400, 401, 402, 404, 409, 429, 500)
    ex.detail().error_type;        // string (e.g. "invalid_request", "unauthenticated")
    ex.detail().message;           // string
    ex.detail().request_id;        // string (for support tracing)
    ex.detail().validation_errors; // vector<ApiValidationError> — present on 400
}
```

---

## Transport layer

The library abstracts I/O behind two interfaces:

| Interface | Default implementation | Platform |
| --- | --- | --- |
| `IHttpTransport` | `CurlHttpTransport` (libcurl) | All |
| `IWebSocketTransport` | `WinHttpWebSocketTransport` (WinHTTP) | Windows |
| `IWebSocketTransport` | `LwsWebSocketTransport` (libwebsockets) | Linux / macOS |

Pass a custom transport to any client constructor to swap backends:

```cpp
auto my_http = std::make_shared<MyHttpTransport>();
soniox::SttRestClient stt("key", my_http);
```

See [docs/transport.md](docs/transport.md) for the full interface contract.

---

## Running the examples

```bash
export SONIOX_API_KEY=your_key

# Real-time STT from file
./build/examples/realtime/soniox_realtime audio.wav
./build/examples/realtime/soniox_realtime audio.wav --lang en,es --diarize

# Real-time STT from microphone (press Enter or Ctrl+C to stop)
./build/examples/realtime_mic/soniox_realtime_mic --lang en --diarize

# Real-time STT with translation (streams original + translated text side by side)
./build/examples/realtime_translation/soniox_realtime_translation audio.wav --src-lang en --tgt-lang es

# Async STT (local file)
./build/examples/async/soniox_async audio.wav --diarize
# Async STT (public URL)
./build/examples/async/soniox_async --url https://example.com/audio.wav

# Async STT with per-token timestamps and confidence scores (tab-separated table)
./build/examples/async_timestamps/soniox_async_timestamps audio.wav --diarize --lang-id
# Same, rendered as SRT subtitles
./build/examples/async_timestamps/soniox_async_timestamps audio.wav --output srt

# TTS REST
./build/examples/tts_rest/soniox_tts_rest --text "Hello from Soniox" --out speech.wav

# TTS real-time
./build/examples/tts_realtime/soniox_tts_realtime --text "Hello from real-time TTS" --out speech_rt.wav

# TTS WebSocket multiplexing (3 concurrent streams — English, Spanish, French)
./build/examples/tts_multiplex/soniox_tts_multiplex

# TTS incremental text streaming (word-by-word, reports time-to-first-audio)
echo "The quick brown fox jumps over the lazy dog" | \
    ./build/examples/tts_streaming_text/soniox_tts_streaming_text --word-delay-ms 100
./build/examples/tts_streaming_text/soniox_tts_streaming_text --file text.txt --voice Maya
```

---

## License

Apache 2.0 — see [LICENSE](LICENSE).
