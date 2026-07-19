#include <sonioxpp/async_client.hpp>
#include <sonioxpp/stt_client.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <stdexcept>
#include <thread>

namespace soniox {
namespace {

using json = nlohmann::json;

json buildContextJson(const Context& context)
{
    json ctx = json::object();
    if (!context.general.empty()) {
        ctx["general"] = context.general;
    }
    if (!context.text.empty()) {
        ctx["text"] = context.text;
    }
    if (!context.terms.empty()) {
        ctx["terms"] = context.terms;
    }
    if (!context.translation_terms.empty()) {
        ctx["translation_terms"] = context.translation_terms;
    }
    return ctx;
}

json buildTranslationJson(const Translation& translation)
{
    json t = json::object();
    if (!translation.type.empty()) {
        t["type"] = translation.type;
    }
    if (translation.type == stt::translation_types::one_way) {
        // language_a kept as a legacy fallback for the one_way target
        const std::string& target = !translation.target_language.empty()
                                    ? translation.target_language : translation.language_a;
        if (!target.empty()) {
            t["target_language"] = target;
        }
    } else {
        if (!translation.language_a.empty()) {
            t["language_a"] = translation.language_a;
        }
        if (!translation.language_b.empty()) {
            t["language_b"] = translation.language_b;
        }
    }
    return t;
}

static std::string jsonStringField(const json& obj, const char* key)
{
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return {};
    return it->get<std::string>();
}

std::string parseRequiredId(const std::string& body, const char* operation)
{
    const json payload = json::parse(body);
    if (!payload.contains("id")) {
        throw std::runtime_error(std::string("[sonioxpp] ") + operation + ": unexpected response");
    }
    return payload["id"].get<std::string>();
}

} // namespace

class AsyncClientImpl {
public:
    explicit AsyncClientImpl(const std::string& apiKey)
        : _restClient(apiKey)
    {
    }

    std::string uploadFile(const std::string& filePath)
    {
        return parseRequiredId(_restClient.uploadFile(filePath), "uploadFile");
    }

    void deleteFile(const std::string& fileId)
    {
        _restClient.deleteFile(fileId);
    }

    std::string createTranscription(const AsyncConfig& config)
    {
        if (config.audio_url.empty() && config.file_id.empty()) {
            throw std::runtime_error("[sonioxpp] createTranscription: set audio_url or file_id in AsyncConfig");
        }

        SttCreateTranscriptionRequest request;
        request.model = config.model;
        request.audio_url = config.audio_url;
        request.file_id = config.file_id;
        request.language_hints = config.language_hints;
        request.language_hints_strict = config.language_hints_strict;
        request.enable_language_identification = config.enable_language_identification;
        request.enable_speaker_diarization = config.enable_speaker_diarization;
        request.webhook_url = config.webhook_url;
        request.webhook_auth_header_name = config.webhook_auth_header_name;
        request.webhook_auth_header_value = config.webhook_auth_header_value;
        request.client_reference_id = config.client_reference_id;

        const json context = buildContextJson(config.context);
        if (!context.empty()) {
            request.context_json = context.dump();
        }

        const json translation = buildTranslationJson(config.translation);
        if (!translation.empty()) {
            request.translation_json = translation.dump();
        }

        return parseRequiredId(_restClient.createTranscription(request), "createTranscription");
    }

    AsyncTranscription getTranscription(const std::string& transcriptionId)
    {
        const json payload = json::parse(_restClient.getTranscription(transcriptionId));

        AsyncTranscription result;
        result.id = jsonStringField(payload, "id");
        if (result.id.empty()) result.id = transcriptionId;
        result.error_message = jsonStringField(payload, "error_message");

        const std::string status = jsonStringField(payload, "status");
        if (status == "completed") {
            result.status = TranscriptionStatus::Completed;
        } else if (status == "running") {
            result.status = TranscriptionStatus::Running;
        } else if (status == "error") {
            result.status = TranscriptionStatus::Error;
        } else {
            result.status = TranscriptionStatus::Pending;
        }

        return result;
    }

    std::vector<Token> getTranscript(const std::string& transcriptionId)
    {
        const json payload = json::parse(_restClient.getTranscriptionTranscript(transcriptionId));

        std::vector<Token> tokens;
        if (!payload.contains("tokens") || !payload["tokens"].is_array()) {
            return tokens;
        }

        for (const auto& item : payload["tokens"]) {
            Token token;
            token.text = jsonStringField(item, "text");
            token.is_final = item.value("is_final", true);
            // API returns speaker as a string label (e.g. "1")
            const std::string speakerStr = jsonStringField(item, "speaker");
            if (!speakerStr.empty()) {
                try { token.speaker = std::stoi(speakerStr); } catch (...) {}
            }
            token.language = jsonStringField(item, "language");
            token.source_language = jsonStringField(item, "source_language");
            token.translation_status = jsonStringField(item, "translation_status");
            token.start_ms = item.value("start_ms", 0);
            token.confidence = item.value("confidence", 0.0);

            if (item.contains("duration_ms") && item["duration_ms"].is_number_integer()) {
                token.duration_ms = item.value("duration_ms", 0);
                token.end_ms = token.start_ms + token.duration_ms;
            } else {
                token.end_ms = item.value("end_ms", token.start_ms);
                token.duration_ms = (token.end_ms >= token.start_ms) ? (token.end_ms - token.start_ms) : 0;
            }

            tokens.push_back(std::move(token));
        }

        return tokens;
    }

    void deleteTranscription(const std::string& transcriptionId)
    {
        _restClient.deleteTranscription(transcriptionId);
    }

    std::vector<Token> transcribeFile(const std::string& filePath, AsyncConfig config, int pollIntervalMs)
    {
        const std::string fileId = uploadFile(filePath);
        config.file_id = fileId;
        config.audio_url.clear();

        const std::string transcriptionId = createTranscription(config);

        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
            const AsyncTranscription status = getTranscription(transcriptionId);

            if (status.status == TranscriptionStatus::Completed) {
                std::vector<Token> tokens = getTranscript(transcriptionId);
                deleteTranscription(transcriptionId);
                deleteFile(fileId);
                return tokens;
            }

            if (status.status == TranscriptionStatus::Error) {
                deleteTranscription(transcriptionId);
                deleteFile(fileId);
                throw std::runtime_error("[sonioxpp] Transcription failed: " + status.error_message);
            }

            spdlog::debug("[sonioxpp] Poll id={} status={}", transcriptionId, static_cast<int>(status.status));
        }
    }

private:
    SttRestClient _restClient;
};

AsyncClient::AsyncClient(const std::string& apiKey)
    : _impl(std::make_unique<AsyncClientImpl>(apiKey))
{
}

AsyncClient::~AsyncClient() = default;
AsyncClient::AsyncClient(AsyncClient&&) noexcept = default;
AsyncClient& AsyncClient::operator=(AsyncClient&&) noexcept = default;

std::string AsyncClient::uploadFile(const std::string& filePath)
{
    return _impl->uploadFile(filePath);
}

void AsyncClient::deleteFile(const std::string& fileId)
{
    _impl->deleteFile(fileId);
}

std::string AsyncClient::createTranscription(const AsyncConfig& config)
{
    return _impl->createTranscription(config);
}

AsyncTranscription AsyncClient::getTranscription(const std::string& transcriptionId)
{
    return _impl->getTranscription(transcriptionId);
}

std::vector<Token> AsyncClient::getTranscript(const std::string& transcriptionId)
{
    return _impl->getTranscript(transcriptionId);
}

void AsyncClient::deleteTranscription(const std::string& transcriptionId)
{
    _impl->deleteTranscription(transcriptionId);
}

std::vector<Token> AsyncClient::transcribeFile(const std::string& filePath, AsyncConfig config, int pollIntervalMs)
{
    return _impl->transcribeFile(filePath, std::move(config), pollIntervalMs);
}

} // namespace soniox
