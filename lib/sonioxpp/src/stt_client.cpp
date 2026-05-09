#include <sonioxpp/stt_client.hpp>
#include <sonioxpp/transport/curl_http_transport.hpp>
#ifdef _WIN32
#include <sonioxpp/transport/winhttp_websocket_transport.hpp>
#else
#include <sonioxpp/transport/lws_websocket_transport.hpp>
#endif
#include <sonioxpp/types.hpp>

#include <nlohmann/json.hpp>

#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace soniox {
namespace {

using json = nlohmann::json;

std::string stringOrEmpty(const json& obj, const char* key)
{
    if (!obj.contains(key) || obj[key].is_null()) {
        return "";
    }
    if (obj[key].is_string()) {
        return obj[key].get<std::string>();
    }
    return obj[key].dump();
}

SttFile parseSttFile(const json& payload)
{
    SttFile file;
    file.id = stringOrEmpty(payload, "id");
    file.filename = stringOrEmpty(payload, "filename");
    file.size = payload.value("size", 0LL);
    file.created_at = stringOrEmpty(payload, "created_at");
    file.client_reference_id = stringOrEmpty(payload, "client_reference_id");
    return file;
}

SttFilesCount parseFilesCount(const json& payload)
{
    SttFilesCount count;
    count.total = payload.value("total", 0);
    count.public_api = payload.value("public_api", 0);
    count.playground = payload.value("playground", 0);
    return count;
}

SttTranscriptionsCount parseTranscriptionsCount(const json& payload)
{
    SttTranscriptionsCount count;
    count.total = payload.value("total", 0);
    count.public_api = payload.value("public_api", 0);
    count.playground = payload.value("playground", 0);
    return count;
}

SttTranscription parseTranscription(const json& payload)
{
    SttTranscription t;
    t.id = stringOrEmpty(payload, "id");
    t.status = stringOrEmpty(payload, "status");
    t.created_at = stringOrEmpty(payload, "created_at");
    t.model = stringOrEmpty(payload, "model");
    t.audio_url = stringOrEmpty(payload, "audio_url");
    t.file_id = stringOrEmpty(payload, "file_id");
    t.filename = stringOrEmpty(payload, "filename");
    t.error_type = stringOrEmpty(payload, "error_type");
    t.error_message = stringOrEmpty(payload, "error_message");
    t.client_reference_id = stringOrEmpty(payload, "client_reference_id");
    return t;
}

SttTranscript parseTranscript(const json& payload)
{
    SttTranscript transcript;
    transcript.id = stringOrEmpty(payload, "id");
    transcript.text = stringOrEmpty(payload, "text");

    if (payload.contains("tokens") && payload["tokens"].is_array()) {
        for (const auto& item : payload["tokens"]) {
            SttTranscriptToken token;
            token.text = stringOrEmpty(item, "text");
            token.start_ms = item.value("start_ms", 0);
            token.end_ms = item.value("end_ms", 0);
            token.confidence = item.value("confidence", 0.0);
            token.speaker = stringOrEmpty(item, "speaker");
            token.language = stringOrEmpty(item, "language");
            token.translation_status = stringOrEmpty(item, "translation_status");
            transcript.tokens.push_back(std::move(token));
        }
    }

    return transcript;
}

std::string urlEncode(const std::string& value)
{
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex << std::uppercase;

    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << static_cast<char>(c);
        } else {
            encoded << '%' << std::setw(2) << static_cast<int>(c);
        }
    }

    return encoded.str();
}

std::string makeQueryString(const PaginationQuery& query)
{
    std::ostringstream out;
    bool first = true;
    if (query.limit > 0) {
        out << (first ? "?" : "&") << "limit=" << query.limit;
        first = false;
    }
    if (!query.cursor.empty()) {
        out << (first ? "?" : "&") << "cursor=" << urlEncode(query.cursor);
    }
    return out.str();
}

void throwIfError(const transport::HttpResponse& response, const std::string& operation)
{
    if (response.status_code >= 400) {
        const std::string body(response.body.begin(), response.body.end());
        ApiErrorDetail detail;
        detail.status_code = static_cast<int>(response.status_code);
        detail.raw_body = body;

        const auto payload = json::parse(body, nullptr, false);
        if (!payload.is_discarded()) {
            detail.status_code = payload.value("status_code", detail.status_code);
            detail.error_type = stringOrEmpty(payload, "error_type");
            detail.message = stringOrEmpty(payload, "message");
            detail.request_id = stringOrEmpty(payload, "request_id");

            if (payload.contains("validation_errors") && payload["validation_errors"].is_array()) {
                for (const auto& ve : payload["validation_errors"]) {
                    ApiValidationError validation;
                    validation.error_type = stringOrEmpty(ve, "error_type");
                    validation.location = stringOrEmpty(ve, "location");
                    validation.message = stringOrEmpty(ve, "message");
                    detail.validation_errors.push_back(std::move(validation));
                }
            }
        }

        throw SonioxApiException(operation, std::move(detail));
    }
}

} // namespace

SttRestClient::SttRestClient(
    std::string api_key,
    std::shared_ptr<transport::IHttpTransport> http_transport,
    std::string base_url)
    : api_key_(std::move(api_key)),
      base_url_(std::move(base_url)),
      http_transport_(std::move(http_transport))
{
    if (!http_transport_) {
        http_transport_ = std::make_shared<transport::CurlHttpTransport>();
    }
}

transport::HttpRequest SttRestClient::makeJsonRequest(
    transport::HttpMethod method,
    const std::string& path,
    const std::string& json_body) const
{
    transport::HttpRequest request;
    request.method = method;
    request.url = buildUrl(path);
    request.headers["Authorization"] = "Bearer " + api_key_;
    request.headers["User-Agent"] = "sonioxpp/2.0";
    if (!json_body.empty()) {
        request.content_type = "application/json";
        request.body = json_body;
    }
    return request;
}

std::string SttRestClient::buildUrl(const std::string& path) const
{
    return base_url_ + path;
}

std::string SttRestClient::toStringBody(const std::vector<std::uint8_t>& body)
{
    return std::string(body.begin(), body.end());
}

std::string SttRestClient::uploadFile(const std::string& file_path, const std::string& client_reference_id)
{
    transport::HttpRequest request;
    request.method = transport::HttpMethod::Post;
    request.url = buildUrl("/v1/files");
    request.headers["Authorization"] = "Bearer " + api_key_;
    request.headers["User-Agent"] = "sonioxpp/2.0";
    request.multipart_files.push_back({"file", file_path, ""});

    if (!client_reference_id.empty()) {
        json body;
        body["client_reference_id"] = client_reference_id;
        request.content_type = "application/json";
        request.body = body.dump();
    }

    const auto response = http_transport_->send(request);
    throwIfError(response, "upload file");
    return toStringBody(response.body);
}

SttFile SttRestClient::uploadFileTyped(const std::string& file_path, const std::string& client_reference_id)
{
    return parseSttFile(json::parse(uploadFile(file_path, client_reference_id)));
}

std::string SttRestClient::getFile(const std::string& file_id)
{
    const auto response = http_transport_->send(makeJsonRequest(transport::HttpMethod::Get, "/v1/files/" + file_id));
    throwIfError(response, "get file");
    return toStringBody(response.body);
}

SttFile SttRestClient::getFileTyped(const std::string& file_id)
{
    return parseSttFile(json::parse(getFile(file_id)));
}

std::string SttRestClient::getFileUrl(const std::string& file_id)
{
    const auto response = http_transport_->send(makeJsonRequest(transport::HttpMethod::Get, "/v1/files/" + file_id + "/url"));
    throwIfError(response, "get file url");
    return toStringBody(response.body);
}

SttFileUrl SttRestClient::getFileUrlTyped(const std::string& file_id)
{
    const auto payload = json::parse(getFileUrl(file_id));
    SttFileUrl url;
    url.url = stringOrEmpty(payload, "url");
    return url;
}

std::string SttRestClient::getFilesCount()
{
    const auto response = http_transport_->send(makeJsonRequest(transport::HttpMethod::Get, "/v1/files/count"));
    throwIfError(response, "get files count");
    return toStringBody(response.body);
}

SttFilesCount SttRestClient::getFilesCountTyped()
{
    return parseFilesCount(json::parse(getFilesCount()));
}

void SttRestClient::deleteFile(const std::string& file_id)
{
    const auto response = http_transport_->send(makeJsonRequest(transport::HttpMethod::Delete, "/v1/files/" + file_id));
    throwIfError(response, "delete file");
}

std::string SttRestClient::listFiles(const PaginationQuery& query)
{
    const auto response = http_transport_->send(makeJsonRequest(
        transport::HttpMethod::Get,
        "/v1/files" + makeQueryString(query)));
    throwIfError(response, "list files");
    return toStringBody(response.body);
}

SttListFilesTypedResult SttRestClient::listFilesTyped(const PaginationQuery& query)
{
    const auto payload = json::parse(listFiles(query));
    SttListFilesTypedResult result;
    result.next_page_cursor = stringOrEmpty(payload, "next_page_cursor");

    if (payload.contains("files") && payload["files"].is_array()) {
        for (const auto& file : payload["files"]) {
            result.files.push_back(parseSttFile(file));
        }
    }

    return result;
}

std::string SttRestClient::createTranscription(const SttCreateTranscriptionRequest& request)
{
    json body;
    body["model"] = request.model;

    if (!request.audio_url.empty()) {
        body["audio_url"] = request.audio_url;
    }
    if (!request.file_id.empty()) {
        body["file_id"] = request.file_id;
    }
    if (!request.language_hints.empty()) {
        body["language_hints"] = request.language_hints;
        body["language_hints_strict"] = request.language_hints_strict;
    }

    body["enable_speaker_diarization"] = request.enable_speaker_diarization;
    body["enable_language_identification"] = request.enable_language_identification;

    if (!request.context_json.empty()) {
        body["context"] = json::parse(request.context_json);
    }
    if (!request.translation_json.empty()) {
        body["translation"] = json::parse(request.translation_json);
    }
    if (!request.webhook_url.empty()) {
        body["webhook_url"] = request.webhook_url;
    }
    if (!request.webhook_auth_header_name.empty()) {
        body["webhook_auth_header_name"] = request.webhook_auth_header_name;
    }
    if (!request.webhook_auth_header_value.empty()) {
        body["webhook_auth_header_value"] = request.webhook_auth_header_value;
    }
    if (!request.client_reference_id.empty()) {
        body["client_reference_id"] = request.client_reference_id;
    }

    const auto response = http_transport_->send(
        makeJsonRequest(transport::HttpMethod::Post, "/v1/transcriptions", body.dump()));
    throwIfError(response, "create transcription");
    return toStringBody(response.body);
}

SttTranscription SttRestClient::createTranscriptionTyped(const SttCreateTranscriptionRequest& request)
{
    return parseTranscription(json::parse(createTranscription(request)));
}

std::string SttRestClient::getTranscription(const std::string& transcription_id)
{
    const auto response = http_transport_->send(
        makeJsonRequest(transport::HttpMethod::Get, "/v1/transcriptions/" + transcription_id));
    throwIfError(response, "get transcription");
    return toStringBody(response.body);
}

SttTranscription SttRestClient::getTranscriptionTyped(const std::string& transcription_id)
{
    return parseTranscription(json::parse(getTranscription(transcription_id)));
}

std::string SttRestClient::getTranscriptionTranscript(const std::string& transcription_id)
{
    const auto response = http_transport_->send(
        makeJsonRequest(transport::HttpMethod::Get, "/v1/transcriptions/" + transcription_id + "/transcript"));
    throwIfError(response, "get transcription transcript");
    return toStringBody(response.body);
}

SttTranscript SttRestClient::getTranscriptionTranscriptTyped(const std::string& transcription_id)
{
    return parseTranscript(json::parse(getTranscriptionTranscript(transcription_id)));
}

std::string SttRestClient::getTranscriptionsCount()
{
    const auto response = http_transport_->send(
        makeJsonRequest(transport::HttpMethod::Get, "/v1/transcriptions/count"));
    throwIfError(response, "get transcriptions count");
    return toStringBody(response.body);
}

SttTranscriptionsCount SttRestClient::getTranscriptionsCountTyped()
{
    return parseTranscriptionsCount(json::parse(getTranscriptionsCount()));
}

void SttRestClient::deleteTranscription(const std::string& transcription_id)
{
    const auto response = http_transport_->send(
        makeJsonRequest(transport::HttpMethod::Delete, "/v1/transcriptions/" + transcription_id));
    throwIfError(response, "delete transcription");
}

std::string SttRestClient::listTranscriptions(const PaginationQuery& query)
{
    const auto response = http_transport_->send(makeJsonRequest(
        transport::HttpMethod::Get,
        "/v1/transcriptions" + makeQueryString(query)));
    throwIfError(response, "list transcriptions");
    return toStringBody(response.body);
}

SttListTranscriptionsTypedResult SttRestClient::listTranscriptionsTyped(const PaginationQuery& query)
{
    const auto payload = json::parse(listTranscriptions(query));
    SttListTranscriptionsTypedResult result;
    result.next_page_cursor = stringOrEmpty(payload, "next_page_cursor");

    if (payload.contains("transcriptions") && payload["transcriptions"].is_array()) {
        for (const auto& t : payload["transcriptions"]) {
            result.transcriptions.push_back(parseTranscription(t));
        }
    }

    return result;
}

std::string SttRestClient::getModels()
{
    const auto response = http_transport_->send(makeJsonRequest(transport::HttpMethod::Get, "/v1/models"));
    throwIfError(response, "get STT models");
    return toStringBody(response.body);
}

SttRealtimeClient::SttRealtimeClient(
    std::shared_ptr<transport::IWebSocketTransport> ws_transport,
    std::string endpoint)
    : endpoint_(std::move(endpoint)),
      ws_transport_(std::move(ws_transport))
{
    if (!ws_transport_) {
#ifdef _WIN32
        ws_transport_ = std::make_shared<transport::WinHttpWebSocketTransport>();
#else
        ws_transport_ = std::make_shared<transport::LwsWebSocketTransport>();
#endif
    }

    ws_transport_->setOnTextMessage([this](const std::string& message) {
        if (on_message_) {
            on_message_(message);
        }
    });

    ws_transport_->setOnError([this](const std::string& error) {
        if (on_error_) {
            on_error_(error);
        }
    });

    ws_transport_->setOnClose([this] {
        if (on_closed_) {
            on_closed_();
        }
    });
}

void SttRealtimeClient::setOnMessage(MessageCallback callback)
{
    on_message_ = std::move(callback);
}

void SttRealtimeClient::setOnError(ErrorCallback callback)
{
    on_error_ = std::move(callback);
}

void SttRealtimeClient::setOnClosed(ClosedCallback callback)
{
    on_closed_ = std::move(callback);
}

std::string SttRealtimeClient::buildConfigJson(const SttRealtimeConfig& config) const
{
    json payload;
    payload["api_key"] = config.api_key;
    payload["model"] = config.model;
    payload["audio_format"] = config.audio_format;

    if (config.sample_rate > 0) {
        payload["sample_rate"] = config.sample_rate;
    }
    if (config.num_channels > 0) {
        payload["num_channels"] = config.num_channels;
    }

    if (!config.language_hints.empty()) {
        payload["language_hints"] = config.language_hints;
        payload["language_hints_strict"] = config.language_hints_strict;
    }

    payload["enable_speaker_diarization"] = config.enable_speaker_diarization;
    payload["enable_language_identification"] = config.enable_language_identification;
    payload["enable_endpoint_detection"] = config.enable_endpoint_detection;

    if (config.max_endpoint_delay_ms > 0) {
        payload["max_endpoint_delay_ms"] = config.max_endpoint_delay_ms;
    }
    if (!config.context_json.empty()) {
        payload["context"] = json::parse(config.context_json);
    }
    if (!config.translation_json.empty()) {
        payload["translation"] = json::parse(config.translation_json);
    }
    if (!config.client_reference_id.empty()) {
        payload["client_reference_id"] = config.client_reference_id;
    }

    return payload.dump();
}

void SttRealtimeClient::connect(const SttRealtimeConfig& config)
{
    transport::WebSocketConnectOptions options;
    options.url = endpoint_;
    options.headers["User-Agent"] = "sonioxpp/2.0";

    ws_transport_->connect(options);
    ws_transport_->sendText(buildConfigJson(config));
}

void SttRealtimeClient::sendAudio(const std::vector<std::uint8_t>& chunk)
{
    ws_transport_->sendBinary(chunk);
}

void SttRealtimeClient::sendEndOfAudio()
{
    ws_transport_->sendBinary({});
}

void SttRealtimeClient::sendManualFinalize()
{
    ws_transport_->sendText("{\"type\":\"finalize\"}");
}

void SttRealtimeClient::sendKeepalive()
{
    ws_transport_->sendText("{\"type\":\"keepalive\"}");
}

void SttRealtimeClient::close()
{
    ws_transport_->close();
}

} // namespace soniox
