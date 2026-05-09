#pragma once

/// @file stt_client.hpp
/// @brief Typed REST and real-time WebSocket clients for the Soniox STT API.
///
/// ### REST base URL
/// `https://api.soniox.com`
///
/// ### WebSocket endpoint
/// `wss://stt-rt.soniox.com/transcribe-websocket`
///
/// All REST operations require `Authorization: Bearer <api_key>`.
/// Real-time sessions include the key in the initial JSON config message.

#include <sonioxpp/api_constants.hpp>
#include <sonioxpp/transport/http_transport.hpp>
#include <sonioxpp/transport/websocket_transport.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace soniox {

// ---------------------------------------------------------------------------
// File management structs  (POST/GET /v1/files)
// ---------------------------------------------------------------------------

/// Metadata for an uploaded audio file.
/// Returned by `uploadFileTyped`, `getFileTyped`, and `listFilesTyped`.
///
/// @see https://soniox.com/docs/speech-to-text/api-reference/files/upload-file
struct SttFile {
    std::string id;                   ///< UUID assigned by the API
    std::string filename;             ///< Original filename as uploaded
    long long   size{0};              ///< File size in bytes
    std::string created_at;           ///< ISO 8601 upload timestamp
    std::string client_reference_id;  ///< Your tracking tag (max 256 chars); empty if not set
};

/// File count breakdown by origin (REST vs. Playground).
/// Returned by `getFilesCountTyped`.
struct SttFilesCount {
    int total{0};       ///< Total number of files owned by the API key
    int public_api{0};  ///< Files created via the REST API
    int playground{0};  ///< Files created via the Soniox Playground
};

/// Transcription count breakdown by origin.
/// Returned by `getTranscriptionsCountTyped`.
struct SttTranscriptionsCount {
    int total{0};       ///< Total number of transcriptions owned by the API key
    int public_api{0};  ///< Transcriptions created via the REST API
    int playground{0};  ///< Transcriptions created via the Soniox Playground
};

/// A short-lived URL for downloading an uploaded audio file.
/// Returned by `getFileUrlTyped`.
struct SttFileUrl {
    std::string url; ///< Pre-signed HTTPS download URL (valid for a limited time)
};

// ---------------------------------------------------------------------------
// Transcription structs  (POST/GET /v1/transcriptions)
// ---------------------------------------------------------------------------

/// Status and metadata for an async transcription job.
/// Returned by `createTranscriptionTyped`, `getTranscriptionTyped`, and `listTranscriptionsTyped`.
///
/// Poll `status` until it is `"completed"` or `"error"`, then call
/// `getTranscriptionTranscriptTyped` to retrieve the result.
///
/// @see https://soniox.com/docs/speech-to-text/api-reference/transcriptions/get-transcription
struct SttTranscription {
    std::string id;                   ///< UUID of the transcription job
    std::string status;               ///< `"queued"`, `"processing"`, `"completed"`, or `"error"`
    std::string created_at;           ///< ISO 8601 creation timestamp
    std::string model;                ///< Model used (e.g. `"stt-async-v4"`)
    std::string audio_url;            ///< Source URL if created from a URL; empty otherwise
    std::string file_id;              ///< Source file UUID if created from an upload; empty otherwise
    std::string filename;             ///< Original filename of the source audio
    std::string error_type;           ///< Machine-readable error category when `status == "error"`
    std::string error_message;        ///< Human-readable error detail when `status == "error"`
    std::string client_reference_id;  ///< Your tracking tag; empty if not set
};

/// A single word/punctuation token in a completed async transcript.
struct SttTranscriptToken {
    std::string text;                   ///< Token text
    int         start_ms{0};           ///< Start time relative to audio start, in milliseconds
    int         end_ms{0};             ///< End time relative to audio start, in milliseconds
    double      confidence{0.0};       ///< Confidence score in [0.0, 1.0]
    std::string speaker;               ///< Speaker label (e.g. `"spk_0"`); empty unless diarization enabled
    std::string language;              ///< BCP-47 detected language; empty unless language ID enabled
    std::string translation_status;    ///< `"none"`, `"original"`, or `"translation"`; empty unless translation enabled
};

/// Complete transcript returned by `getTranscriptionTranscriptTyped`.
///
/// Only accessible once `SttTranscription::status == "completed"`.
/// @see https://soniox.com/docs/speech-to-text/api-reference/transcriptions/get-transcription-transcript
struct SttTranscript {
    std::string id;                          ///< Transcription UUID
    std::string text;                        ///< Full concatenated transcript text
    std::vector<SttTranscriptToken> tokens;  ///< Per-token detail (timing, speaker, language)
};

// ---------------------------------------------------------------------------
// Pagination
// ---------------------------------------------------------------------------

/// Page parameters for `listFilesTyped` and `listTranscriptionsTyped`.
struct PaginationQuery {
    int         limit{0};   ///< Maximum items to return (1–1000; 0 = server default of 1000)
    std::string cursor;     ///< Opaque cursor from a previous `next_page_cursor`; empty = first page
};

/// Paginated result of `listFilesTyped`.
struct SttListFilesTypedResult {
    std::vector<SttFile> files;      ///< Files on this page
    std::string next_page_cursor;    ///< Pass to the next call to advance the page; empty when done
};

/// Paginated result of `listTranscriptionsTyped`.
struct SttListTranscriptionsTypedResult {
    std::vector<SttTranscription> transcriptions; ///< Transcriptions on this page
    std::string next_page_cursor;                 ///< Pass to the next call; empty when done
};

/// Raw (untyped) paginated file list (values as JSON strings).
struct SttListFilesResult {
    std::vector<std::map<std::string, std::string>> files;
    std::string next_page_cursor;
};

/// Raw (untyped) paginated transcription list.
struct SttListTranscriptionsResult {
    std::vector<std::map<std::string, std::string>> transcriptions;
    std::string next_page_cursor;
};

// ---------------------------------------------------------------------------
// Transcription request
// ---------------------------------------------------------------------------

/// Request body for `POST /v1/transcriptions`.
///
/// Exactly one of `file_id` or `audio_url` must be non-empty.
/// Serialised to JSON by `SttRestClient::createTranscription`.
///
/// @see https://soniox.com/docs/speech-to-text/api-reference/transcriptions/create-transcription
struct SttCreateTranscriptionRequest {
    std::string model{stt::models::async_v4}; ///< Model ID (default: `stt-async-v4`)

    /// @name Audio source — set exactly one
    /// @{
    std::string audio_url; ///< Public HTTPS URL to the audio (max 4096 chars)
    std::string file_id;   ///< UUID of a previously uploaded file
    /// @}

    /// @name Language options
    /// @{
    std::vector<std::string> language_hints;    ///< BCP-47 language codes (max 100); guides decoding
    bool language_hints_strict{false};           ///< When true, disables fallback to other languages
    /// @}

    /// @name Feature flags
    /// @{
    bool enable_speaker_diarization{false};      ///< Assign speaker IDs to each token
    bool enable_language_identification{false};  ///< Tag each token with its detected language
    /// @}

    /// @name Context and translation (pre-serialised JSON strings)
    /// Build these with `nlohmann::json` or the `Context` / `Translation` helpers.
    /// @{
    std::string context_json;     ///< Serialised `StructuredContext` JSON object
    std::string translation_json; ///< Serialised `TranslationConfig` JSON object
    /// @}

    /// @name Webhook (optional)
    /// When set, Soniox POSTs the completed `Transcription` JSON to this URL.
    /// @{
    std::string webhook_url;                  ///< HTTPS URL to receive the completion callback (max 256 chars)
    std::string webhook_auth_header_name;     ///< Auth header name sent with the webhook (max 256 chars)
    std::string webhook_auth_header_value;    ///< Auth header value (masked in API responses)
    /// @}

    std::string client_reference_id; ///< Opaque tracking tag (max 256 chars)
};

// ---------------------------------------------------------------------------
// SttRestClient
// ---------------------------------------------------------------------------

/// Typed REST client for the Soniox STT API.
///
/// Covers file management (`/v1/files`) and transcription lifecycle
/// (`/v1/transcriptions`).  Each operation is available in two flavours:
///  - **Raw** — returns the response body as a `std::string` (JSON).
///  - **Typed** — parses the JSON and returns a struct (e.g. `SttFile`).
///
/// All methods throw `SonioxApiException` on HTTP >= 400 responses and
/// `std::runtime_error` on transport-level failures.
///
/// ### Typical workflow
/// @code
///   soniox::SttRestClient stt("YOUR_API_KEY");
///
///   // Upload
///   soniox::SttFile file = stt.uploadFileTyped("audio.wav");
///
///   // Transcribe
///   soniox::SttTranscription tx = stt.createTranscriptionTyped({
///       .model          = soniox::stt::models::async_v4,
///       .file_id        = file.id,
///       .language_hints = {"en"},
///   });
///
///   // Poll
///   while (tx.status == "queued" || tx.status == "processing") {
///       std::this_thread::sleep_for(std::chrono::seconds(1));
///       tx = stt.getTranscriptionTyped(tx.id);
///   }
///
///   // Retrieve
///   soniox::SttTranscript result = stt.getTranscriptionTranscriptTyped(tx.id);
///
///   // Clean up
///   stt.deleteTranscription(tx.id);
///   stt.deleteFile(file.id);
/// @endcode
class SttRestClient {
public:
    /// @param api_key        Soniox API key (Bearer token).
    /// @param http_transport Custom transport; nullptr = use `CurlHttpTransport`.
    /// @param base_url       API base URL (override for regional endpoints or testing).
    explicit SttRestClient(
        std::string api_key,
        std::shared_ptr<transport::IHttpTransport> http_transport = nullptr,
        std::string base_url = "https://api.soniox.com");

    // -----------------------------------------------------------------------
    // File management  — POST/GET/DELETE /v1/files
    // -----------------------------------------------------------------------

    /// Upload a local audio file. Returns the raw JSON body.
    /// @param file_path            Path to the audio file.
    /// @param client_reference_id  Optional tracking tag (max 256 chars).
    std::string uploadFile(const std::string& file_path, const std::string& client_reference_id = "");

    /// Upload a local audio file. Returns a typed `SttFile`.
    SttFile uploadFileTyped(const std::string& file_path, const std::string& client_reference_id = "");

    /// Retrieve file metadata. Returns the raw JSON body.
    std::string getFile(const std::string& file_id);

    /// Retrieve file metadata. Returns a typed `SttFile`.
    SttFile getFileTyped(const std::string& file_id);

    /// Retrieve a short-lived download URL for a file. Returns the raw JSON body.
    std::string getFileUrl(const std::string& file_id);

    /// Retrieve a short-lived download URL. Returns a typed `SttFileUrl`.
    SttFileUrl getFileUrlTyped(const std::string& file_id);

    /// Get total file counts by origin. Returns the raw JSON body.
    std::string getFilesCount();

    /// Get total file counts. Returns a typed `SttFilesCount`.
    SttFilesCount getFilesCountTyped();

    /// Delete a file. Throws `SonioxApiException` if not found (404).
    void deleteFile(const std::string& file_id);

    /// List files with optional pagination. Returns the raw JSON body.
    std::string listFiles(const PaginationQuery& query = {});

    /// List files with optional pagination. Returns a typed result.
    SttListFilesTypedResult listFilesTyped(const PaginationQuery& query = {});

    // -----------------------------------------------------------------------
    // Transcription lifecycle  — POST/GET/DELETE /v1/transcriptions
    // -----------------------------------------------------------------------

    /// Create a transcription job. Returns the raw JSON body.
    std::string createTranscription(const SttCreateTranscriptionRequest& request);

    /// Create a transcription job. Returns a typed `SttTranscription`.
    SttTranscription createTranscriptionTyped(const SttCreateTranscriptionRequest& request);

    /// Poll transcription status. Returns the raw JSON body.
    std::string getTranscription(const std::string& transcription_id);

    /// Poll transcription status. Returns a typed `SttTranscription`.
    SttTranscription getTranscriptionTyped(const std::string& transcription_id);

    /// Retrieve the completed transcript tokens. Returns the raw JSON body.
    /// @throws SonioxApiException (409) if the job is not yet completed.
    std::string getTranscriptionTranscript(const std::string& transcription_id);

    /// Retrieve the completed transcript. Returns a typed `SttTranscript`.
    SttTranscript getTranscriptionTranscriptTyped(const std::string& transcription_id);

    /// Get total transcription counts. Returns the raw JSON body.
    std::string getTranscriptionsCount();

    /// Get total transcription counts. Returns a typed `SttTranscriptionsCount`.
    SttTranscriptionsCount getTranscriptionsCountTyped();

    /// Delete a transcription job. Throws `SonioxApiException` (409) if still processing.
    void deleteTranscription(const std::string& transcription_id);

    /// List transcriptions with optional pagination. Returns the raw JSON body.
    std::string listTranscriptions(const PaginationQuery& query = {});

    /// List transcriptions with optional pagination. Returns a typed result.
    SttListTranscriptionsTypedResult listTranscriptionsTyped(const PaginationQuery& query = {});

    // -----------------------------------------------------------------------
    // Models
    // -----------------------------------------------------------------------

    /// Returns the raw JSON list of available STT models (`GET /v1/models`).
    std::string getModels();

private:
    transport::HttpRequest makeJsonRequest(
        transport::HttpMethod method,
        const std::string& path,
        const std::string& json_body = "") const;

    std::string buildUrl(const std::string& path) const;
    static std::string toStringBody(const std::vector<std::uint8_t>& body);

    std::string _apiKey;
    std::string _baseUrl;
    std::shared_ptr<transport::IHttpTransport> _httpTransport;
};

// ---------------------------------------------------------------------------
// Real-time WebSocket client
// ---------------------------------------------------------------------------

/// Configuration for a Soniox real-time STT WebSocket session.
///
/// Sent as a JSON text frame immediately after the connection is established.
/// @see https://soniox.com/docs/speech-to-text/api-reference/realtime-transcription
struct SttRealtimeConfig {
    std::string api_key;                                         ///< API key or temporary key (required)
    std::string model{stt::models::realtime_v4};                 ///< Model ID (default: `stt-rt-v4`)
    std::string audio_format{stt::audio_formats::auto_detect};   ///< Audio format (default: `"auto"`)
    int sample_rate{0};     ///< PCM sample rate in Hz; required when `audio_format` is a raw PCM format
    int num_channels{0};    ///< Number of channels; required when `audio_format` is a raw PCM format

    /// @name Language options
    /// @{
    std::vector<std::string> language_hints;    ///< BCP-47 language codes to guide decoding
    bool language_hints_strict{false};           ///< Restrict decoding to hinted languages only
    /// @}

    /// @name Feature flags
    /// @{
    bool enable_speaker_diarization{false};      ///< Assign speaker IDs to each token
    bool enable_language_identification{false};  ///< Tag each token with its detected language
    bool enable_endpoint_detection{false};       ///< Emit `<end>` tokens at natural speech pauses
    int  max_endpoint_delay_ms{0};               ///< Maximum delay before an endpoint is emitted (500–3000 ms; 0 = server default of 2000)
    /// @}

    /// @name Context and translation (pre-serialised JSON strings)
    /// @{
    std::string context_json;     ///< Serialised `StructuredContext` object
    std::string translation_json; ///< Serialised `TranslationConfig` object
    /// @}

    std::string client_reference_id; ///< Opaque tracking tag (max 256 chars)
};

/// Low-level typed WebSocket client for Soniox real-time STT.
///
/// Delivers raw JSON message strings to a callback; parsing is the caller's
/// responsibility.  For a higher-level client with typed callbacks, use
/// `RealtimeClient` instead.
///
/// ### Protocol summary
///  1. Register callbacks with `setOn*`.
///  2. Call `connect(config)` — sends the JSON config to the server.
///  3. Stream audio via `sendAudio`.
///  4. Call `sendEndOfAudio()` when all audio has been sent.
///  5. The server sends a final message with `"finished": true`.
///  6. `ClosedCallback` fires and the connection is torn down.
///
/// ### Server message format (JSON)
/// ```json
/// {
///   "tokens": [
///     { "text": "Hello", "is_final": true, "speaker": 0,
///       "language": "en", "translation_status": "original" }
///   ],
///   "final_audio_proc_ms": 1200,
///   "total_audio_proc_ms": 1800,
///   "finished": false
/// }
/// ```
///
/// Error messages: `{ "error_code": <int>, "error_message": "<string>" }`
///
/// @see https://soniox.com/docs/speech-to-text/api-reference/realtime-transcription
class SttRealtimeClient {
public:
    /// Called for each JSON text frame received from the server.
    using MessageCallback = std::function<void(const std::string&)>;

    /// Called when the transport reports an unrecoverable error.
    using ErrorCallback = std::function<void(const std::string&)>;

    /// Called once the WebSocket connection is fully closed.
    using ClosedCallback = std::function<void()>;

    /// @param ws_transport  Custom WebSocket backend; nullptr = platform default.
    /// @param endpoint      STT WebSocket URL (override for regional endpoints).
    explicit SttRealtimeClient(
        std::shared_ptr<transport::IWebSocketTransport> ws_transport = nullptr,
        std::string endpoint = "wss://stt-rt.soniox.com/transcribe-websocket");

    /// @name Callbacks — register before calling connect()
    /// @{
    void setOnMessage(MessageCallback callback);  ///< Raw JSON message handler
    void setOnError(ErrorCallback callback);       ///< Transport error handler
    void setOnClosed(ClosedCallback callback);     ///< Connection closed handler
    /// @}

    /// Connect and send the initial JSON configuration to the server.
    /// Blocks until the handshake completes.
    /// @throws std::runtime_error on connection failure.
    void connect(const SttRealtimeConfig& config);

    /// Send a binary audio frame.
    void sendAudio(const std::vector<std::uint8_t>& chunk);

    /// Send an empty binary frame to signal end-of-audio.
    /// The server finalises any remaining tokens and sends `"finished": true`.
    void sendEndOfAudio();

    /// Request immediate finalisation of all pending tokens.
    /// Sends `{"type": "finalize"}`.  Do not call more often than every few seconds.
    void sendManualFinalize();

    /// Send a keepalive frame to prevent session timeout.
    /// Sends `{"type": "keepalive"}`.  Call every 5–10 s during silence.
    /// The server closes the connection after 20 s of inactivity (no audio + no keepalive).
    void sendKeepalive();

    /// Close the WebSocket connection.
    void close();

private:
    std::string buildConfigJson(const SttRealtimeConfig& config) const;

    std::string _endpoint;
    std::shared_ptr<transport::IWebSocketTransport> _wsTransport;

    MessageCallback _onMessage;
    ErrorCallback _onError;
    ClosedCallback _onClosed;
};

} // namespace soniox
