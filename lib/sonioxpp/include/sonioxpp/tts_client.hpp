#pragma once

#include <sonioxpp/api_constants.hpp>
#include <sonioxpp/transport/http_transport.hpp>
#include <sonioxpp/transport/websocket_transport.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace soniox {

struct TtsModelLanguage {
    std::string code;
    std::string name;
};

struct TtsModelVoice {
    std::string id;
};

struct TtsModel {
    std::string id;
    std::string aliased_model_id;
    std::string name;
    std::vector<TtsModelLanguage> languages;
    std::vector<TtsModelVoice> voices;
};

struct TtsModelsResponse {
    std::vector<TtsModel> models;
};

struct TtsRealtimeMessage {
    std::string stream_id;
    std::string audio;
    bool terminated{false};
    int error_code{0};
    std::string error_message;
    std::string raw_message;
};

struct TtsGenerateRequest {
    std::string model{tts::models::realtime_v1};
    std::string language;
    std::string voice;
    std::string audio_format;
    std::string text;
    int sample_rate{0};
    int bitrate{0};
    std::string request_id;
};

class TtsRestClient {
public:
    explicit TtsRestClient(
        std::string api_key,
        std::shared_ptr<transport::IHttpTransport> http_transport = nullptr,
        std::string api_base_url = "https://api.soniox.com",
        std::string tts_base_url = "https://tts-rt.soniox.com");

    transport::HttpResponse generateSpeech(const TtsGenerateRequest& request);
    std::string getModels();
    TtsModelsResponse getModelsTyped();

private:
    std::string api_key_;
    std::string api_base_url_;
    std::string tts_base_url_;
    std::shared_ptr<transport::IHttpTransport> http_transport_;
};

struct TtsRealtimeStreamConfig {
    std::string api_key;
    std::string stream_id;
    std::string model{tts::models::realtime_v1};
    std::string language;
    std::string voice;
    std::string audio_format;
    int sample_rate{0};
    int bitrate{0};
};

class TtsRealtimeClient {
public:
    using TextMessageCallback = std::function<void(const std::string&)>;
    using ParsedMessageCallback = std::function<void(const TtsRealtimeMessage&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using ClosedCallback = std::function<void()>;

    explicit TtsRealtimeClient(
        std::shared_ptr<transport::IWebSocketTransport> ws_transport = nullptr,
        std::string endpoint = "wss://tts-rt.soniox.com/tts-websocket");

    void setOnMessage(TextMessageCallback callback);
    void setOnParsedMessage(ParsedMessageCallback callback);
    void setOnError(ErrorCallback callback);
    void setOnClosed(ClosedCallback callback);

    void connect();
    void startStream(const TtsRealtimeStreamConfig& config);
    void sendText(const std::string& stream_id, const std::string& text, bool text_end);
    void cancelStream(const std::string& stream_id);
    void sendKeepalive();
    void close();

    TtsRealtimeMessage parseMessage(const std::string& message) const;

private:
    std::string endpoint_;
    std::shared_ptr<transport::IWebSocketTransport> ws_transport_;

    TextMessageCallback on_message_;
    ParsedMessageCallback on_parsed_message_;
    ErrorCallback on_error_;
    ClosedCallback on_closed_;
};

} // namespace soniox
