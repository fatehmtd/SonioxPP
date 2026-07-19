#pragma once

#include <sonioxpp/api_constants.hpp>
#include <sonioxpp/types.hpp>
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

struct TtsModelLanguage {
    std::string code;
    std::string name;
};

struct TtsModelVoice {
    std::string id;
};

struct TtsModel {
    std::string id;
    std::string aliased_model_id; ///< non-empty when this entry is an alias
    std::string name;
    std::vector<TtsModelLanguage> languages;
    std::vector<TtsModelVoice>   voices;
};

struct TtsModelsResponse {
    std::vector<TtsModel> models;
};

// ---------------------------------------------------------------------------
// Voices  (POST/GET/DELETE /v1/voices)
// ---------------------------------------------------------------------------

/// Preparation status of a cloned voice on one model.
struct TtsVoiceModelStatus {
    std::string model;
    std::string status;        ///< e.g. "not_computed" when the model was released after the voice
    std::string error_type;
    std::string error_message;
};

/// A cloned voice created from a reference audio clip.
struct TtsVoice {
    std::string id;
    std::string name;
    std::string filename;
    std::string created_at;
    std::vector<TtsVoiceModelStatus> models;
};

struct TtsVoicesCount {
    int total{0};
};

struct TtsListVoicesTypedResult {
    std::vector<TtsVoice> voices;
    std::string next_page_cursor;
};

// ---------------------------------------------------------------------------
// Real-time TTS message
// ---------------------------------------------------------------------------

/// Character-level audio alignment (present when return_timestamps is enabled).
struct TtsCharacterTimestamps {
    std::vector<std::string> characters;
    std::vector<double>      start_times_seconds;
    std::vector<double>      end_times_seconds;
};

/// A server-sent message on the TTS real-time WebSocket.
struct TtsRealtimeMessage {
    std::string stream_id;
    std::string audio;         ///< base64-encoded; empty for non-audio messages
    bool        audio_end{false};  ///< last audio chunk for the stream
    bool        terminated{false}; ///< stream_id may be reused after this
    TtsCharacterTimestamps timestamps;
    int         error_code{0};
    std::string error_type;
    std::string error_message;
    std::string request_id;
    std::string raw_message;
};

// ---------------------------------------------------------------------------
// REST TTS request  (POST https://tts-rt.soniox.com/tts)
// ---------------------------------------------------------------------------

/// Mid-stream errors are propagated via X-Tts-Error-Code / X-Tts-Error-Message trailers.
struct TtsGenerateRequest {
    std::string model{tts::models::realtime_v1};
    std::string language;
    std::string voice;          ///< built-in voice name or cloned voice ID
    std::string audio_format;
    std::string text;           ///< max 5000 characters
    int         sample_rate{0}; ///< 0 = format default (24 000 Hz)
    int         bitrate{0};     ///< 0 = format default; lossy formats only
    double      speed{0.0};     ///< 0.7–1.3; 0.0 = server default of 1.0
    std::string client_reference_id;
    std::string request_id;
};

// ---------------------------------------------------------------------------
// TtsRestClient
// ---------------------------------------------------------------------------

/// REST client for Soniox Text-to-Speech.
class TtsRestClient {
public:
    explicit TtsRestClient(
        std::string api_key,
        std::shared_ptr<transport::IHttpTransport> http_transport = nullptr,
        std::string api_base_url = "https://api.soniox.com",
        std::string tts_base_url = "https://tts-rt.soniox.com");

    /* Returns HttpResponse whose body contains raw audio bytes.
       Check trailers["X-Tts-Error-Code"] for mid-stream errors.
       Throws SonioxApiException on HTTP >= 400. */
    transport::HttpResponse generateSpeech(const TtsGenerateRequest& request);

    std::string       getModels();
    TtsModelsResponse getModelsTyped();

    // Voice cloning  — POST/GET/DELETE /v1/voices

    /// Uploads a reference audio clip; name must be unique (max 128 chars).
    std::string createVoice(const std::string& name, const std::string& file_path);
    TtsVoice    createVoiceTyped(const std::string& name, const std::string& file_path);

    std::string              listVoices(const PaginationQuery& query = {});
    TtsListVoicesTypedResult listVoicesTyped(const PaginationQuery& query = {});

    std::string    getVoicesCount();
    TtsVoicesCount getVoicesCountTyped();

    std::string getVoice(const std::string& voice_id);
    TtsVoice    getVoiceTyped(const std::string& voice_id);

    /// Prepares a voice for models released after it was created.
    std::string recomputeVoice(const std::string& voice_id);
    TtsVoice    recomputeVoiceTyped(const std::string& voice_id);

    void deleteVoice(const std::string& voice_id);

private:
    transport::HttpRequest makeApiRequest(
        transport::HttpMethod method,
        const std::string& path) const;

    std::string _apiKey;
    std::string _apiBaseUrl;
    std::string _ttsBaseUrl;
    std::shared_ptr<transport::IHttpTransport> _httpTransport;
};

// ---------------------------------------------------------------------------
// Real-time TTS WebSocket  (wss://tts-rt.soniox.com/tts-websocket)
// ---------------------------------------------------------------------------

/* Stream-start config sent as the first text frame for a stream.
   Up to 5 streams can be active simultaneously on a single connection. */
struct TtsRealtimeStreamConfig {
    std::string api_key;
    std::string stream_id;        ///< reusable after terminated message
    std::string model{tts::models::realtime_v1};
    std::string language;
    std::string voice;            ///< built-in voice name or cloned voice ID
    std::string audio_format;
    int         sample_rate{0};   ///< 0 = format default
    int         bitrate{0};       ///< 0 = format default; lossy formats only
    double      speed{0.0};       ///< 0.7–1.3; 0.0 = server default of 1.0
    bool        return_timestamps{false}; ///< enable character-level audio alignment
    std::string client_reference_id;
};

/* Event-driven WebSocket client for Soniox real-time TTS.
   Stream errors do not close the WebSocket; other streams continue unaffected.
   Call sendKeepalive() every 20–30 s when all streams are idle to prevent timeout. */
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

    explicit TtsRealtimeClient(
        std::shared_ptr<transport::IWebSocketTransport> ws_transport = nullptr,
        std::string endpoint = "wss://tts-rt.soniox.com/tts-websocket");

    void setOnMessage(TextMessageCallback callback);
    void setOnParsedMessage(ParsedMessageCallback callback);
    void setOnError(ErrorCallback callback);
    void setOnClosed(ClosedCallback callback);

    /* Authentication occurs in startStream via the api_key field.
       Throws std::runtime_error on connection failure. */
    void connect();

    void startStream(const TtsRealtimeStreamConfig& config);

    /// Set text_end=true on the final chunk to signal completion.
    void sendText(const std::string& stream_id, const std::string& text, bool text_end);

    /// Server responds with a terminated message for the stream.
    void cancelStream(const std::string& stream_id);

    void sendKeepalive();
    void close();

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
