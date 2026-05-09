#include <sonioxpp/tts_client.hpp>
#include <sonioxpp/transport/curl_http_transport.hpp>
#ifdef _WIN32
#include <sonioxpp/transport/winhttp_websocket_transport.hpp>
#else
#include <sonioxpp/transport/lws_websocket_transport.hpp>
#endif
#include <sonioxpp/types.hpp>

#include <nlohmann/json.hpp>

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

TtsModelsResponse parseTtsModelsResponse(const json& payload)
{
    TtsModelsResponse out;
    if (!payload.contains("models") || !payload["models"].is_array()) {
        return out;
    }

    for (const auto& m : payload["models"]) {
        TtsModel model;
        model.id = stringOrEmpty(m, "id");
        model.aliased_model_id = stringOrEmpty(m, "aliased_model_id");
        model.name = stringOrEmpty(m, "name");

        if (m.contains("languages") && m["languages"].is_array()) {
            for (const auto& lang : m["languages"]) {
                TtsModelLanguage language;
                language.code = stringOrEmpty(lang, "code");
                language.name = stringOrEmpty(lang, "name");
                model.languages.push_back(std::move(language));
            }
        }

        if (m.contains("voices") && m["voices"].is_array()) {
            for (const auto& v : m["voices"]) {
                TtsModelVoice voice;
                voice.id = stringOrEmpty(v, "id");
                model.voices.push_back(std::move(voice));
            }
        }

        out.models.push_back(std::move(model));
    }

    return out;
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
            detail.status_code = payload.value("status_code", payload.value("error_code", detail.status_code));
            detail.error_type = stringOrEmpty(payload, "error_type");
            detail.message = stringOrEmpty(payload, "message");
            if (detail.message.empty()) {
                detail.message = stringOrEmpty(payload, "error_message");
            }
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

TtsRestClient::TtsRestClient(
    std::string api_key,
    std::shared_ptr<transport::IHttpTransport> http_transport,
    std::string api_base_url,
    std::string tts_base_url)
    : _apiKey(std::move(api_key)),
      _apiBaseUrl(std::move(api_base_url)),
      _ttsBaseUrl(std::move(tts_base_url)),
      _httpTransport(std::move(http_transport))
{
    if (!_httpTransport) {
        _httpTransport = std::make_shared<transport::CurlHttpTransport>();
    }
}

transport::HttpResponse TtsRestClient::generateSpeech(const TtsGenerateRequest& request)
{
    json body;
    body["model"] = request.model;
    body["language"] = request.language;
    body["voice"] = request.voice;
    body["audio_format"] = request.audio_format;
    body["text"] = request.text;

    if (request.sample_rate > 0) {
        body["sample_rate"] = request.sample_rate;
    }
    if (request.bitrate > 0) {
        body["bitrate"] = request.bitrate;
    }

    transport::HttpRequest http_request;
    http_request.method = transport::HttpMethod::Post;
    http_request.url = _ttsBaseUrl + "/tts";
    http_request.content_type = "application/json";
    http_request.body = body.dump();
    http_request.headers["Authorization"] = "Bearer " + _apiKey;
    http_request.headers["User-Agent"] = soniox::USER_AGENT;
    if (!request.request_id.empty()) {
        http_request.headers["X-Request-Id"] = request.request_id;
    }

    auto response = _httpTransport->send(http_request);
    throwIfError(response, "generate TTS");
    return response;
}

std::string TtsRestClient::getModels()
{
    transport::HttpRequest request;
    request.method = transport::HttpMethod::Get;
    request.url = _apiBaseUrl + "/v1/tts-models";
    request.headers["Authorization"] = "Bearer " + _apiKey;
    request.headers["User-Agent"] = soniox::USER_AGENT;

    const auto response = _httpTransport->send(request);
    throwIfError(response, "get TTS models");

    return std::string(response.body.begin(), response.body.end());
}

TtsModelsResponse TtsRestClient::getModelsTyped()
{
    return parseTtsModelsResponse(json::parse(getModels()));
}

TtsRealtimeClient::TtsRealtimeClient(
    std::shared_ptr<transport::IWebSocketTransport> ws_transport,
    std::string endpoint)
    : _endpoint(std::move(endpoint)),
      _wsTransport(std::move(ws_transport))
{
    if (!_wsTransport) {
#ifdef _WIN32
        _wsTransport = std::make_shared<transport::WinHttpWebSocketTransport>();
#else
        _wsTransport = std::make_shared<transport::LwsWebSocketTransport>();
#endif
    }

    _wsTransport->setOnTextMessage([this](const std::string& message) {
        if (_onMessage) {
            _onMessage(message);
        }

        if (_onParsedMessage) {
            _onParsedMessage(parseMessage(message));
        }
    });

    _wsTransport->setOnError([this](const std::string& message) {
        if (_onError) {
            _onError(message);
        }
    });

    _wsTransport->setOnClose([this] {
        if (_onClosed) {
            _onClosed();
        }
    });
}

void TtsRealtimeClient::setOnMessage(TextMessageCallback callback)
{
    _onMessage = std::move(callback);
}

void TtsRealtimeClient::setOnParsedMessage(ParsedMessageCallback callback)
{
    _onParsedMessage = std::move(callback);
}

void TtsRealtimeClient::setOnError(ErrorCallback callback)
{
    _onError = std::move(callback);
}

void TtsRealtimeClient::setOnClosed(ClosedCallback callback)
{
    _onClosed = std::move(callback);
}

void TtsRealtimeClient::connect()
{
    transport::WebSocketConnectOptions options;
    options.url = _endpoint;
    options.headers["User-Agent"] = soniox::USER_AGENT;
    _wsTransport->connect(options);
}

void TtsRealtimeClient::startStream(const TtsRealtimeStreamConfig& config)
{
    json payload;
    payload["api_key"] = config.api_key;
    payload["stream_id"] = config.stream_id;
    payload["model"] = config.model;
    payload["language"] = config.language;
    payload["voice"] = config.voice;
    payload["audio_format"] = config.audio_format;

    if (config.sample_rate > 0) {
        payload["sample_rate"] = config.sample_rate;
    }
    if (config.bitrate > 0) {
        payload["bitrate"] = config.bitrate;
    }

    _wsTransport->sendText(payload.dump());
}

void TtsRealtimeClient::sendText(const std::string& stream_id, const std::string& text, bool text_end)
{
    json payload;
    payload["stream_id"] = stream_id;
    payload["text"] = text;
    payload["text_end"] = text_end;
    _wsTransport->sendText(payload.dump());
}

void TtsRealtimeClient::cancelStream(const std::string& stream_id)
{
    json payload;
    payload["stream_id"] = stream_id;
    payload["cancel"] = true;
    _wsTransport->sendText(payload.dump());
}

void TtsRealtimeClient::sendKeepalive()
{
    json payload;
    payload["keep_alive"] = true;
    _wsTransport->sendText(payload.dump());
}

void TtsRealtimeClient::close()
{
    _wsTransport->close();
}

TtsRealtimeMessage TtsRealtimeClient::parseMessage(const std::string& message) const
{
    TtsRealtimeMessage parsed;
    parsed.raw_message = message;

    const json payload = json::parse(message, nullptr, false);
    if (payload.is_discarded()) {
        return parsed;
    }

    parsed.stream_id = stringOrEmpty(payload, "stream_id");
    parsed.audio = stringOrEmpty(payload, "audio");
    parsed.terminated = payload.value("terminated", false);
    parsed.error_code = payload.value("error_code", 0);
    parsed.error_message = stringOrEmpty(payload, "error_message");
    return parsed;
}

} // namespace soniox
