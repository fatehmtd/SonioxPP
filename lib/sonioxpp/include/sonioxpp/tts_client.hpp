#pragma once

/// @file tts_client.hpp
/// @brief REST and real-time WebSocket clients for the Soniox Text-to-Speech API.
///
/// ### Endpoints
/// | Client              | Endpoint                                    |
/// |---------------------|---------------------------------------------|
/// | `TtsRestClient`     | `POST https://tts-rt.soniox.com/tts`        |
/// | `TtsRestClient`     | `GET  https://api.soniox.com/v1/models`     |
/// | `TtsRealtimeClient` | `wss://tts-rt.soniox.com/tts-websocket`     |
///
/// All requests require `Authorization: Bearer <api_key>`.
/// Real-time sessions include the key in the initial stream-start JSON message.
///
/// @see https://soniox.com/docs/text-to-speech

#include <sonioxpp/api_constants.hpp>
#include <sonioxpp/transport/http_transport.hpp>
#include <sonioxpp/transport/websocket_transport.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace soniox {

// ---------------------------------------------------------------------------
// Models response  (GET /v1/models)
// ---------------------------------------------------------------------------

/// A language supported by a TTS model.
struct TtsModelLanguage {
    std::string code; ///< BCP-47 language code (e.g. `"en"`)
    std::string name; ///< Human-readable language name (e.g. `"English"`)
};

/// A voice available within a TTS model.
struct TtsModelVoice {
    std::string id; ///< Voice identifier (e.g. `"Adrian"`, `"Maya"`)
};

/// Metadata for a single TTS model.
struct TtsModel {
    std::string id;               ///< Model ID (e.g. `"tts-rt-v1"`)
    std::string aliased_model_id; ///< Underlying model ID when this entry is an alias; empty otherwise
    std::string name;             ///< Human-readable model name
    std::vector<TtsModelLanguage> languages; ///< Languages supported by the model
    std::vector<TtsModelVoice>   voices;    ///< Voices available for the model
};

/// Response from `GET /v1/models` (TTS).
struct TtsModelsResponse {
    std::vector<TtsModel> models; ///< All available TTS models
};

// ---------------------------------------------------------------------------
// Real-time TTS message
// ---------------------------------------------------------------------------

/// A server-sent message on the TTS real-time WebSocket.
///
/// The server sends one or more audio chunks followed by a termination message
/// for each stream.  Parse the raw JSON string using `TtsRealtimeClient::parseMessage`.
///
/// ### Typical message flow (single stream)
/// ```
/// {"audio": "<base64>", "audio_end": false, "stream_id": "s1"}
/// {"audio": "<base64>", "audio_end": true,  "stream_id": "s1"}
/// {"terminated": true,                       "stream_id": "s1"}
/// ```
///
/// On error:
/// ```
/// {"stream_id": "s1", "error_code": 400, "error_message": "..."}
/// {"terminated": true, "stream_id": "s1"}
/// ```
struct TtsRealtimeMessage {
    std::string stream_id;      ///< Stream this message belongs to
    std::string audio;          ///< Base64-encoded audio bytes; empty for non-audio messages
    bool        terminated{false};  ///< `true` on the final message for this stream;
                                    ///< safe to reuse or release the `stream_id` after this
    int         error_code{0};      ///< Non-zero on stream error (400, 401, 402, 408, 429, 500, 503)
    std::string error_message;      ///< Human-readable error description; non-empty when `error_code != 0`
    std::string raw_message;        ///< Original JSON text of the message
};

// ---------------------------------------------------------------------------
// REST TTS request  (POST https://tts-rt.soniox.com/tts)
// ---------------------------------------------------------------------------

/// Request body for `POST https://tts-rt.soniox.com/tts`.
///
/// The response body contains raw audio bytes in the requested `audio_format`.
/// TTS errors that occur after headers are sent are propagated via HTTP trailers:
/// `X-Tts-Error-Code` and `X-Tts-Error-Message`.
///
/// ### Available voices
/// `Maya`, `Daniel`, `Noah`, `Nina`, `Emma`, `Jack`,
/// `Adrian`, `Claire`, `Grace`, `Owen`, `Mina`, `Kenji`
///
/// All voices support 60+ languages.
///
/// @see https://soniox.com/docs/text-to-speech/api-reference/rest-api
struct TtsGenerateRequest {
    std::string model{tts::models::realtime_v1}; ///< Model ID (default: `tts-rt-v1`)
    std::string language;       ///< BCP-47 language code (required, e.g. `"en"`)
    std::string voice;          ///< Voice name (required, e.g. `"Adrian"`)
    std::string audio_format;   ///< Output format: `wav`, `mp3`, `opus`, `flac`, `pcm_s16le`, etc. (required)
    std::string text;           ///< Text to synthesise (required, max 5 000 chars)
    int         sample_rate{0}; ///< Output sample rate in Hz; 0 = format default (typically 24 000 Hz)
    int         bitrate{0};     ///< Output bitrate in bps for lossy formats (`mp3`, `opus`, `aac`); 0 = format default
    std::string request_id;     ///< Optional client-assigned request ID for tracing
};

// ---------------------------------------------------------------------------
// TtsRestClient
// ---------------------------------------------------------------------------

/// REST client for Soniox Text-to-Speech.
///
/// ### Example — generate and save WAV
/// @code
///   soniox::TtsRestClient tts("YOUR_API_KEY");
///
///   soniox::TtsGenerateRequest req;
///   req.language     = "en";
///   req.voice        = "Adrian";
///   req.audio_format = soniox::tts::audio_formats::wav;
///   req.text         = "Hello from Soniox.";
///
///   auto resp = tts.generateSpeech(req);
///   // resp.body contains raw WAV bytes
///   std::ofstream f("out.wav", std::ios::binary);
///   f.write(reinterpret_cast<const char*>(resp.body.data()), resp.body.size());
/// @endcode
class TtsRestClient {
public:
    /// @param api_key        Soniox API key.
    /// @param http_transport Custom transport; nullptr = use `CurlHttpTransport`.
    /// @param api_base_url   Base URL for model listing (default: `https://api.soniox.com`).
    /// @param tts_base_url   Base URL for speech generation (default: `https://tts-rt.soniox.com`).
    explicit TtsRestClient(
        std::string api_key,
        std::shared_ptr<transport::IHttpTransport> http_transport = nullptr,
        std::string api_base_url = "https://api.soniox.com",
        std::string tts_base_url = "https://tts-rt.soniox.com");

    /// Generate speech from text.
    ///
    /// @return `HttpResponse` whose `body` contains the raw audio bytes.
    ///         Check `trailers["X-Tts-Error-Code"]` for mid-stream errors.
    /// @throws SonioxApiException on HTTP >= 400 responses.
    transport::HttpResponse generateSpeech(const TtsGenerateRequest& request);

    /// Return the raw JSON list of available TTS models (`GET /v1/models`).
    std::string getModels();

    /// Return the parsed list of available TTS models.
    TtsModelsResponse getModelsTyped();

private:
    std::string _apiKey;
    std::string _apiBaseUrl;
    std::string _ttsBaseUrl;
    std::shared_ptr<transport::IHttpTransport> _httpTransport;
};

// ---------------------------------------------------------------------------
// Real-time TTS WebSocket  (wss://tts-rt.soniox.com/tts-websocket)
// ---------------------------------------------------------------------------

/// Stream-start config sent via `TtsRealtimeClient::startStream`.
///
/// Serialised to JSON and sent as the first text frame for a stream.
/// Up to **5 streams** can be active simultaneously on a single connection.
///
/// @see https://soniox.com/docs/text-to-speech/api-reference/realtime-api
struct TtsRealtimeStreamConfig {
    std::string api_key;                         ///< Soniox API key or temporary key (required)
    std::string stream_id;                       ///< Unique stream identifier per active stream (max 256 chars; reusable after `terminated`)
    std::string model{tts::models::realtime_v1}; ///< Model ID (default: `tts-rt-v1`)
    std::string language;                        ///< BCP-47 language code (required, e.g. `"en"`)
    std::string voice;                           ///< Voice name (required, e.g. `"Adrian"`)
    std::string audio_format;                    ///< Output format (required)
    int         sample_rate{0};                  ///< Sample rate in Hz; 0 = format default
    int         bitrate{0};                      ///< Bitrate in bps for lossy formats; 0 = format default
};

/// Event-driven WebSocket client for Soniox real-time Text-to-Speech.
///
/// ### Multiplexed streams
/// A single WebSocket connection supports up to **5 concurrent streams**,
/// each identified by a `stream_id`.  Audio and text frames are interleaved —
/// synthesis begins before all text is received.
///
/// ### Typical usage
/// @code
///   soniox::TtsRealtimeClient tts;
///
///   tts.setOnParsedMessage([](const soniox::TtsRealtimeMessage& m) {
///       if (!m.audio.empty()) { /* base64-decode and play m.audio */ }
///       if (m.terminated)    { /* stream done; stream_id may be reused */ }
///       if (m.error_code)    { std::cerr << m.error_message << "\n"; }
///   });
///
///   tts.connect();
///
///   soniox::TtsRealtimeStreamConfig cfg;
///   cfg.api_key      = "YOUR_KEY";
///   cfg.stream_id    = "stream-1";
///   cfg.language     = "en";
///   cfg.voice        = "Adrian";
///   cfg.audio_format = soniox::tts::audio_formats::wav;
///
///   tts.startStream(cfg);
///   tts.sendText("stream-1", "Hello from real-time TTS.", /*text_end=*/true);
///   // wait for terminated callback…
///   tts.close();
/// @endcode
///
/// @note Stream errors do not close the WebSocket; other streams continue unaffected.
/// @note Send `sendKeepalive()` every 20–30 s when all streams are idle to prevent timeout.
class TtsRealtimeClient {
public:
    /// Called for each raw JSON text frame received from the server.
    using TextMessageCallback = std::function<void(const std::string&)>;

    /// Called for each parsed server message.
    using ParsedMessageCallback = std::function<void(const TtsRealtimeMessage&)>;

    /// Called when the transport reports an unrecoverable error.
    using ErrorCallback = std::function<void(const std::string&)>;

    /// Called once the WebSocket connection is fully closed.
    using ClosedCallback = std::function<void()>;

    /// @param ws_transport  Custom WebSocket backend; nullptr = platform default.
    /// @param endpoint      TTS WebSocket URL (override for regional endpoints).
    explicit TtsRealtimeClient(
        std::shared_ptr<transport::IWebSocketTransport> ws_transport = nullptr,
        std::string endpoint = "wss://tts-rt.soniox.com/tts-websocket");

    /// @name Callbacks — register before calling connect()
    /// @{
    void setOnMessage(TextMessageCallback callback);        ///< Raw JSON frame handler
    void setOnParsedMessage(ParsedMessageCallback callback); ///< Parsed message handler (preferred)
    void setOnError(ErrorCallback callback);                ///< Transport error handler
    void setOnClosed(ClosedCallback callback);              ///< Connection closed handler
    /// @}

    /// Open the WebSocket connection (no authentication at this stage).
    /// Authentication occurs in `startStream` via the `api_key` field.
    /// @throws std::runtime_error on connection failure.
    void connect();

    /// Start a new TTS stream on the open connection.
    /// Sends the stream-start JSON config message.
    /// @param config  Stream parameters including `api_key` and `stream_id`.
    void startStream(const TtsRealtimeStreamConfig& config);

    /// Send a text chunk for synthesis.
    ///
    /// @param stream_id  Target stream identifier.
    /// @param text       Text to synthesise (this chunk, max 5 000 chars total per stream).
    /// @param text_end   Set `true` on the final chunk; the server will send `audio_end: true`
    ///                   followed by `terminated: true` once all audio is delivered.
    void sendText(const std::string& stream_id, const std::string& text, bool text_end);

    /// Cancel an active stream.
    /// Sends `{"stream_id": "<id>", "cancel": true}`.
    /// The server will respond with a `terminated` message for the stream.
    void cancelStream(const std::string& stream_id);

    /// Send a connection-level keepalive to prevent idle timeout.
    /// Sends `{"keep_alive": true}`.  Call every 20–30 s when no streams are active.
    void sendKeepalive();

    /// Close the WebSocket connection.
    void close();

    /// Parse a raw JSON string into a `TtsRealtimeMessage`.
    TtsRealtimeMessage parseMessage(const std::string& message) const;

private:
    std::string _endpoint;
    std::shared_ptr<transport::IWebSocketTransport> _wsTransport;

    TextMessageCallback   _onMessage;
    ParsedMessageCallback _onParsedMessage;
    ErrorCallback         _onError;
    ClosedCallback        _onClosed;
};

} // namespace soniox
