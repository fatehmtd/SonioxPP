#pragma once

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
// Real-time TTS message
// ---------------------------------------------------------------------------

/// A server-sent message on the TTS real-time WebSocket.
struct TtsRealtimeMessage {
    std::string stream_id;
    std::string audio;         ///< base64-encoded; empty for non-audio messages
    bool        terminated{false}; ///< stream_id may be reused after this
    int         error_code{0};
    std::string error_message;
    std::string raw_message;
};

// ---------------------------------------------------------------------------
// REST TTS request  (POST https://tts-rt.soniox.com/tts)
// ---------------------------------------------------------------------------

/// Mid-stream errors are propagated via X-Tts-Error-Code / X-Tts-Error-Message trailers.
struct TtsGenerateRequest {
    std::string model{tts::models::realtime_v1};
    std::string language;
    std::string voice;
    std::string audio_format;
    std::string text;
    int         sample_rate{0}; ///< 0 = format default (24 000 Hz)
    int         bitrate{0};     ///< 0 = format default; lossy formats only
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

private:
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
    std::string voice;
    std::string audio_format;
    int         sample_rate{0};   ///< 0 = format default
    int         bitrate{0};       ///< 0 = format default; lossy formats only
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
