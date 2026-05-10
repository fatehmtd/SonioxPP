#pragma once

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

struct SttFile {
    std::string id;
    std::string filename;
    long long   size{0};
    std::string created_at;
    std::string client_reference_id;
};

/// Count breakdown by origin (REST vs. Playground).
struct SttFilesCount {
    int total{0};
    int public_api{0};
    int playground{0};
};

/// Count breakdown by origin (REST vs. Playground).
struct SttTranscriptionsCount {
    int total{0};
    int public_api{0};
    int playground{0};
};

/// Pre-signed HTTPS download URL (valid for a limited time).
struct SttFileUrl {
    std::string url;
};

// ---------------------------------------------------------------------------
// Transcription structs  (POST/GET /v1/transcriptions)
// ---------------------------------------------------------------------------

/// Poll status until "completed" or "error".
struct SttTranscription {
    std::string id;
    std::string status;   ///< "queued", "processing", "completed", or "error"
    std::string created_at;
    std::string model;
    std::string audio_url;
    std::string file_id;
    std::string filename;
    std::string error_type;
    std::string error_message;
    std::string client_reference_id;
};

struct SttTranscriptToken {
    std::string text;
    int         start_ms{0};
    int         end_ms{0};
    double      confidence{0.0};
    std::string speaker;
    std::string language;
    std::string translation_status;
};

/// Only accessible once SttTranscription::status == "completed".
struct SttTranscript {
    std::string id;
    std::string text;
    std::vector<SttTranscriptToken> tokens;
};

// ---------------------------------------------------------------------------
// Pagination
// ---------------------------------------------------------------------------

struct PaginationQuery {
    int         limit{0};  ///< 1–1000; 0 = server default of 1000
    std::string cursor;    ///< opaque cursor from next_page_cursor; empty = first page
};

struct SttListFilesTypedResult {
    std::vector<SttFile> files;
    std::string next_page_cursor;
};

struct SttListTranscriptionsTypedResult {
    std::vector<SttTranscription> transcriptions;
    std::string next_page_cursor;
};

struct SttListFilesResult {
    std::vector<std::map<std::string, std::string>> files;
    std::string next_page_cursor;
};

struct SttListTranscriptionsResult {
    std::vector<std::map<std::string, std::string>> transcriptions;
    std::string next_page_cursor;
};

// ---------------------------------------------------------------------------
// Transcription request
// ---------------------------------------------------------------------------

/// Set exactly one of audio_url or file_id.
struct SttCreateTranscriptionRequest {
    std::string model{stt::models::async_v4};

    std::string audio_url;
    std::string file_id;

    std::vector<std::string> language_hints;
    bool language_hints_strict{false};

    bool enable_speaker_diarization{false};
    bool enable_language_identification{false};

    std::string context_json;
    std::string translation_json;

    std::string webhook_url;
    std::string webhook_auth_header_name;
    std::string webhook_auth_header_value;

    std::string client_reference_id;
};

// ---------------------------------------------------------------------------
// SttRestClient
// ---------------------------------------------------------------------------

/* Typed REST client for the Soniox STT API (/v1/files, /v1/transcriptions).
   Each operation has a raw (JSON string) and a Typed (struct) variant.
   All methods throw SonioxApiException on HTTP >= 400. */
class SttRestClient {
public:
    explicit SttRestClient(
        std::string api_key,
        std::shared_ptr<transport::IHttpTransport> http_transport = nullptr,
        std::string base_url = "https://api.soniox.com");

    // File management  — POST/GET/DELETE /v1/files

    std::string uploadFile(const std::string& file_path, const std::string& client_reference_id = "");
    SttFile     uploadFileTyped(const std::string& file_path, const std::string& client_reference_id = "");

    std::string getFile(const std::string& file_id);
    SttFile     getFileTyped(const std::string& file_id);

    std::string getFileUrl(const std::string& file_id);
    SttFileUrl  getFileUrlTyped(const std::string& file_id);

    std::string   getFilesCount();
    SttFilesCount getFilesCountTyped();

    void deleteFile(const std::string& file_id);

    std::string             listFiles(const PaginationQuery& query = {});
    SttListFilesTypedResult listFilesTyped(const PaginationQuery& query = {});

    // Transcription lifecycle  — POST/GET/DELETE /v1/transcriptions

    std::string      createTranscription(const SttCreateTranscriptionRequest& request);
    SttTranscription createTranscriptionTyped(const SttCreateTranscriptionRequest& request);

    std::string      getTranscription(const std::string& transcription_id);
    SttTranscription getTranscriptionTyped(const std::string& transcription_id);

    /// @throws SonioxApiException (409) if the job is not yet completed.
    std::string   getTranscriptionTranscript(const std::string& transcription_id);
    SttTranscript getTranscriptionTranscriptTyped(const std::string& transcription_id);

    std::string            getTranscriptionsCount();
    SttTranscriptionsCount getTranscriptionsCountTyped();

    /// @throws SonioxApiException (409) if still processing.
    void deleteTranscription(const std::string& transcription_id);

    std::string                      listTranscriptions(const PaginationQuery& query = {});
    SttListTranscriptionsTypedResult listTranscriptionsTyped(const PaginationQuery& query = {});

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

/// Sent as a JSON text frame immediately after the WebSocket connection is established.
struct SttRealtimeConfig {
    std::string api_key;
    std::string model{stt::models::realtime_v4};
    std::string audio_format{stt::audio_formats::auto_detect};
    int sample_rate{0};     ///< required when audio_format is pcm_s16le/pcm_s16be
    int num_channels{0};    ///< required when audio_format is pcm_s16le/pcm_s16be

    std::vector<std::string> language_hints;
    bool language_hints_strict{false};

    bool enable_speaker_diarization{false};
    bool enable_language_identification{false};
    bool enable_endpoint_detection{false};
    int  max_endpoint_delay_ms{0}; ///< 500–3000 ms; 0 = server default of 2000

    std::string context_json;
    std::string translation_json;

    std::string client_reference_id;
};

/* Low-level WebSocket client for Soniox real-time STT — delivers raw JSON to a callback.
   For a higher-level client with typed callbacks, use RealtimeClient instead. */
class SttRealtimeClient {
public:
    /// Called for each JSON text frame received from the server.
    using MessageCallback = std::function<void(const std::string&)>;

    /// Called when the transport reports an unrecoverable error.
    using ErrorCallback = std::function<void(const std::string&)>;

    /// Called once the WebSocket connection is fully closed.
    using ClosedCallback = std::function<void()>;

    explicit SttRealtimeClient(
        std::shared_ptr<transport::IWebSocketTransport> ws_transport = nullptr,
        std::string endpoint = "wss://stt-rt.soniox.com/transcribe-websocket");

    void setOnMessage(MessageCallback callback);
    void setOnError(ErrorCallback callback);
    void setOnClosed(ClosedCallback callback);

    /// @throws std::runtime_error on connection failure.
    void connect(const SttRealtimeConfig& config);

    void sendAudio(const std::vector<std::uint8_t>& chunk);

    /// Send an empty binary frame to signal end-of-audio.
    void sendEndOfAudio();

    /// Sends {"type": "finalize"}. Do not call more often than every few seconds.
    void sendManualFinalize();

    /// Call every 5–10 s during silence; server times out after 20 s of inactivity.
    void sendKeepalive();

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
