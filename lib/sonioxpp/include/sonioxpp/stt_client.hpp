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

struct SttFile {
    std::string id;
    std::string filename;
    long long size{0};
    std::string created_at;
    std::string client_reference_id;
};

struct SttFilesCount {
    int total{0};
    int public_api{0};
    int playground{0};
};

struct SttTranscriptionsCount {
    int total{0};
    int public_api{0};
    int playground{0};
};

struct SttFileUrl {
    std::string url;
};

struct SttTranscription {
    std::string id;
    std::string status;
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
    int start_ms{0};
    int end_ms{0};
    double confidence{0.0};
    std::string speaker;
    std::string language;
    std::string translation_status;
};

struct SttTranscript {
    std::string id;
    std::string text;
    std::vector<SttTranscriptToken> tokens;
};

struct SttListFilesTypedResult {
    std::vector<SttFile> files;
    std::string next_page_cursor;
};

struct SttListTranscriptionsTypedResult {
    std::vector<SttTranscription> transcriptions;
    std::string next_page_cursor;
};

struct PaginationQuery {
    int limit{0};
    std::string cursor;
};

struct SttListFilesResult {
    std::vector<std::map<std::string, std::string>> files;
    std::string next_page_cursor;
};

struct SttListTranscriptionsResult {
    std::vector<std::map<std::string, std::string>> transcriptions;
    std::string next_page_cursor;
};

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

class SttRestClient {
public:
    explicit SttRestClient(
        std::string api_key,
        std::shared_ptr<transport::IHttpTransport> http_transport = nullptr,
        std::string base_url = "https://api.soniox.com");

    std::string uploadFile(const std::string& file_path, const std::string& client_reference_id = "");
    SttFile uploadFileTyped(const std::string& file_path, const std::string& client_reference_id = "");
    std::string getFile(const std::string& file_id);
    SttFile getFileTyped(const std::string& file_id);
    std::string getFileUrl(const std::string& file_id);
    SttFileUrl getFileUrlTyped(const std::string& file_id);
    std::string getFilesCount();
    SttFilesCount getFilesCountTyped();
    void deleteFile(const std::string& file_id);
    std::string listFiles(const PaginationQuery& query = {});
    SttListFilesTypedResult listFilesTyped(const PaginationQuery& query = {});

    std::string createTranscription(const SttCreateTranscriptionRequest& request);
    SttTranscription createTranscriptionTyped(const SttCreateTranscriptionRequest& request);
    std::string getTranscription(const std::string& transcription_id);
    SttTranscription getTranscriptionTyped(const std::string& transcription_id);
    std::string getTranscriptionTranscript(const std::string& transcription_id);
    SttTranscript getTranscriptionTranscriptTyped(const std::string& transcription_id);
    std::string getTranscriptionsCount();
    SttTranscriptionsCount getTranscriptionsCountTyped();
    void deleteTranscription(const std::string& transcription_id);
    std::string listTranscriptions(const PaginationQuery& query = {});
    SttListTranscriptionsTypedResult listTranscriptionsTyped(const PaginationQuery& query = {});

    std::string getModels();

private:
    transport::HttpRequest makeJsonRequest(
        transport::HttpMethod method,
        const std::string& path,
        const std::string& json_body = "") const;

    std::string buildUrl(const std::string& path) const;
    static std::string toStringBody(const std::vector<std::uint8_t>& body);

    std::string api_key_;
    std::string base_url_;
    std::shared_ptr<transport::IHttpTransport> http_transport_;
};

struct SttRealtimeConfig {
    std::string api_key;
    std::string model{stt::models::realtime_v4};
    std::string audio_format{stt::audio_formats::auto_detect};
    int sample_rate{0};
    int num_channels{0};
    std::vector<std::string> language_hints;
    bool language_hints_strict{false};
    bool enable_speaker_diarization{false};
    bool enable_language_identification{false};
    bool enable_endpoint_detection{false};
    int max_endpoint_delay_ms{0};
    std::string context_json;
    std::string translation_json;
    std::string client_reference_id;
};

class SttRealtimeClient {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using ClosedCallback = std::function<void()>;

    explicit SttRealtimeClient(
        std::shared_ptr<transport::IWebSocketTransport> ws_transport = nullptr,
        std::string endpoint = "wss://stt-rt.soniox.com/transcribe-websocket");

    void setOnMessage(MessageCallback callback);
    void setOnError(ErrorCallback callback);
    void setOnClosed(ClosedCallback callback);

    void connect(const SttRealtimeConfig& config);
    void sendAudio(const std::vector<std::uint8_t>& chunk);
    void sendEndOfAudio();
    void sendManualFinalize();
    void sendKeepalive();
    void close();

private:
    std::string buildConfigJson(const SttRealtimeConfig& config) const;

    std::string endpoint_;
    std::shared_ptr<transport::IWebSocketTransport> ws_transport_;

    MessageCallback on_message_;
    ErrorCallback on_error_;
    ClosedCallback on_closed_;
};

} // namespace soniox
