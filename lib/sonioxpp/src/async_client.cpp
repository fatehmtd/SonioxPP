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
    if (!translation.language_a.empty()) {
        t["language_a"] = translation.language_a;
    }
    if (!translation.language_b.empty()) {
        t["language_b"] = translation.language_b;
    }
    return t;
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
        : rest_client_(apiKey)
    {
    }

    std::string uploadFile(const std::string& filePath)
    {
        return parseRequiredId(rest_client_.uploadFile(filePath), "uploadFile");
    }

    void deleteFile(const std::string& fileId)
    {
        rest_client_.deleteFile(fileId);
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
        request.enable_language_identification = config.enable_language_identification;
        request.enable_speaker_diarization = config.enable_speaker_diarization;

        const json context = buildContextJson(config.context);
        if (!context.empty()) {
            request.context_json = context.dump();
        }

        const json translation = buildTranslationJson(config.translation);
        if (!translation.empty()) {
            request.translation_json = translation.dump();
        }

        return parseRequiredId(rest_client_.createTranscription(request), "createTranscription");
    }

    AsyncTranscription getTranscription(const std::string& transcriptionId)
    {
        const json payload = json::parse(rest_client_.getTranscription(transcriptionId));

        AsyncTranscription result;
        result.id = payload.value("id", transcriptionId);
        result.error_message = payload.value("error_message", std::string());

        const std::string status = payload.value("status", std::string("pending"));
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
        const json payload = json::parse(rest_client_.getTranscriptionTranscript(transcriptionId));

        std::vector<Token> tokens;
        if (!payload.contains("tokens") || !payload["tokens"].is_array()) {
            return tokens;
        }

        for (const auto& item : payload["tokens"]) {
            Token token;
            token.text = item.value("text", std::string());
            token.is_final = item.value("is_final", true);
            token.speaker = item.value("speaker", 0);
            token.language = item.value("language", std::string());
            token.translation_status = item.value("translation_status", std::string());
            token.start_ms = item.value("start_ms", 0);

            if (item.contains("duration_ms") && item["duration_ms"].is_number_integer()) {
                token.duration_ms = item.value("duration_ms", 0);
            } else {
                const int end_ms = item.value("end_ms", token.start_ms);
                token.duration_ms = (end_ms >= token.start_ms) ? (end_ms - token.start_ms) : 0;
            }

            tokens.push_back(std::move(token));
        }

        return tokens;
    }

    void deleteTranscription(const std::string& transcriptionId)
    {
        rest_client_.deleteTranscription(transcriptionId);
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
    SttRestClient rest_client_;
};

AsyncClient::AsyncClient(const std::string& apiKey)
    : impl_(std::make_unique<AsyncClientImpl>(apiKey))
{
}

AsyncClient::~AsyncClient() = default;
AsyncClient::AsyncClient(AsyncClient&&) noexcept = default;
AsyncClient& AsyncClient::operator=(AsyncClient&&) noexcept = default;

std::string AsyncClient::uploadFile(const std::string& filePath)
{
    return impl_->uploadFile(filePath);
}

void AsyncClient::deleteFile(const std::string& fileId)
{
    impl_->deleteFile(fileId);
}

std::string AsyncClient::createTranscription(const AsyncConfig& config)
{
    return impl_->createTranscription(config);
}

AsyncTranscription AsyncClient::getTranscription(const std::string& transcriptionId)
{
    return impl_->getTranscription(transcriptionId);
}

std::vector<Token> AsyncClient::getTranscript(const std::string& transcriptionId)
{
    return impl_->getTranscript(transcriptionId);
}

void AsyncClient::deleteTranscription(const std::string& transcriptionId)
{
    impl_->deleteTranscription(transcriptionId);
}

std::vector<Token> AsyncClient::transcribeFile(const std::string& filePath, AsyncConfig config, int pollIntervalMs)
{
    return impl_->transcribeFile(filePath, std::move(config), pollIntervalMs);
}

} // namespace soniox
