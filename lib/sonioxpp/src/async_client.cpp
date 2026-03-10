#include <sonioxpp/async_client.hpp>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace net  = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;
namespace ssl   = boost::asio::ssl;
using     tcp   = net::ip::tcp;
using     json  = nlohmann::json;

namespace soniox {

// ---------------------------------------------------------------------------
// API endpoint constants
// ---------------------------------------------------------------------------

namespace endpoints {
    constexpr const char* AsyncHost = "api.soniox.com";
    constexpr const char* AsyncPort = "443";

    constexpr const char* FilesPath          = "/v1/files";
    constexpr const char* TranscriptionsPath = "/v1/transcriptions";
} // namespace endpoints

// ---------------------------------------------------------------------------
// Multipart boundary
// ---------------------------------------------------------------------------

namespace multipart {
    constexpr const char* Boundary = "----SonioxPPBoundary7MA4YWxk";
} // namespace multipart

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

class AsyncClientImpl {
public:
    explicit AsyncClientImpl(const std::string& apiKey) : _apiKey(apiKey) {}

    // -----------------------------------------------------------------------
    std::string uploadFile(const std::string& filePath)
    {
        std::ifstream audioFile(filePath, std::ios::binary);
        if (!audioFile.is_open())
            throw std::runtime_error("[sonioxpp] Cannot open file: " + filePath);

        std::string audioData(
            (std::istreambuf_iterator<char>(audioFile)),
            std::istreambuf_iterator<char>());

        auto separatorPos = filePath.find_last_of("/\\");
        std::string fileName =
            (separatorPos != std::string::npos)
                ? filePath.substr(separatorPos + 1)
                : filePath;

        // Build multipart/form-data body
        std::string multipartBody;
        multipartBody += std::string("--") + multipart::Boundary + "\r\n";
        multipartBody += "Content-Disposition: form-data; name=\"file\"; filename=\""
                       + fileName + "\"\r\n";
        multipartBody += "Content-Type: application/octet-stream\r\n\r\n";
        multipartBody += audioData;
        multipartBody += std::string("\r\n--") + multipart::Boundary + "--\r\n";

        spdlog::info("[sonioxpp] Uploading {} ({} bytes)", fileName, audioData.size());

        std::string responseBody = httpRequest(
            http::verb::post,
            endpoints::FilesPath,
            "multipart/form-data; boundary=" + std::string(multipart::Boundary),
            multipartBody);

        json jsonResponse = json::parse(responseBody);
        if (!jsonResponse.contains("id"))
            throw std::runtime_error(
                "[sonioxpp] uploadFile: unexpected response: " + responseBody);

        std::string fileId = jsonResponse["id"].get<std::string>();
        spdlog::info("[sonioxpp] File uploaded, id={}", fileId);
        return fileId;
    }

    // -----------------------------------------------------------------------
    void deleteFile(const std::string& fileId)
    {
        httpRequest(http::verb::delete_,
                    std::string(endpoints::FilesPath) + "/" + fileId, "", "");
        spdlog::debug("[sonioxpp] Deleted file id={}", fileId);
    }

    // -----------------------------------------------------------------------
    std::string createTranscription(const AsyncConfig& config)
    {
        if (config.audio_url.empty() && config.file_id.empty())
            throw std::runtime_error(
                "[sonioxpp] createTranscription: set audio_url or file_id in AsyncConfig");

        json jsonContext;
        jsonContext["general"]      = config.context.general;
        jsonContext["text_context"] = config.context.text;
        jsonContext["terms"]        = config.context.terms;

        json jsonTranscriptionReq;
        jsonTranscriptionReq["model"]                          = config.model;
        jsonTranscriptionReq["language_hints"]                 = config.language_hints;
        jsonTranscriptionReq["enable_language_identification"] = config.enable_language_identification;
        jsonTranscriptionReq["enable_speaker_diarization"]     = config.enable_speaker_diarization;
        jsonTranscriptionReq["context"]                        = jsonContext;

        if (!config.audio_url.empty())
            jsonTranscriptionReq["audio_url"] = config.audio_url;
        else
            jsonTranscriptionReq["file_id"] = config.file_id;

        if (!config.translation.type.empty()) {
            json jsonTranslation;
            jsonTranslation["type"] = config.translation.type;
            if (!config.translation.language_a.empty())
                jsonTranslation["language_a"] = config.translation.language_a;
            if (!config.translation.language_b.empty())
                jsonTranslation["language_b"] = config.translation.language_b;
            jsonTranscriptionReq["translation"] = jsonTranslation;
        }

        std::string responseBody = httpRequest(
            http::verb::post,
            endpoints::TranscriptionsPath,
            "application/json",
            jsonTranscriptionReq.dump());

        json jsonResponse = json::parse(responseBody);
        if (!jsonResponse.contains("id"))
            throw std::runtime_error(
                "[sonioxpp] createTranscription: unexpected response: " + responseBody);

        std::string transcriptionId = jsonResponse["id"].get<std::string>();
        spdlog::info("[sonioxpp] Transcription created, id={}", transcriptionId);
        return transcriptionId;
    }

    // -----------------------------------------------------------------------
    AsyncTranscription getTranscription(const std::string& transcriptionId)
    {
        std::string responseBody = httpRequest(
            http::verb::get,
            std::string(endpoints::TranscriptionsPath) + "/" + transcriptionId,
            "", "");

        json jsonResponse = json::parse(responseBody);

        AsyncTranscription transcription;
        transcription.id            = jsonResponse.value("id", transcriptionId);
        transcription.error_message = jsonResponse.value("error_message", "");

        const std::string statusStr = jsonResponse.value("status", "pending");
        if      (statusStr == "completed") transcription.status = TranscriptionStatus::Completed;
        else if (statusStr == "running")   transcription.status = TranscriptionStatus::Running;
        else if (statusStr == "error")     transcription.status = TranscriptionStatus::Error;
        else                               transcription.status = TranscriptionStatus::Pending;

        return transcription;
    }

    // -----------------------------------------------------------------------
    std::vector<Token> getTranscript(const std::string& transcriptionId)
    {
        std::string responseBody = httpRequest(
            http::verb::get,
            std::string(endpoints::TranscriptionsPath) + "/" + transcriptionId + "/transcript",
            "", "");

        json jsonResponse = json::parse(responseBody);

        std::vector<Token> tokens;
        if (jsonResponse.contains("tokens")) {
            for (const auto& jsonToken : jsonResponse["tokens"]) {
                Token token;
                token.text               = jsonToken.value("text", "");
                token.is_final           = jsonToken.value("is_final", true);
                token.speaker            = jsonToken.value("speaker", 0);
                token.language           = jsonToken.value("language", "");
                token.translation_status = jsonToken.value("translation_status", "");
                token.start_ms           = jsonToken.value("start_ms", 0);
                token.duration_ms        = jsonToken.value("duration_ms", 0);
                tokens.push_back(std::move(token));
            }
        }
        return tokens;
    }

    // -----------------------------------------------------------------------
    void deleteTranscription(const std::string& transcriptionId)
    {
        httpRequest(http::verb::delete_,
                    std::string(endpoints::TranscriptionsPath) + "/" + transcriptionId,
                    "", "");
        spdlog::debug("[sonioxpp] Deleted transcription id={}", transcriptionId);
    }

    // -----------------------------------------------------------------------
    std::vector<Token> transcribeFile(
        const std::string& filePath, AsyncConfig config, int pollIntervalMs)
    {
        std::string fileId = uploadFile(filePath);
        config.file_id  = fileId;
        config.audio_url.clear();

        std::string transcriptionId = createTranscription(config);

        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
            AsyncTranscription transcription = getTranscription(transcriptionId);
            spdlog::debug("[sonioxpp] Poll id={} status={}",
                          transcriptionId, static_cast<int>(transcription.status));

            if (transcription.status == TranscriptionStatus::Completed) {
                std::vector<Token> tokens = getTranscript(transcriptionId);
                deleteTranscription(transcriptionId);
                deleteFile(fileId);
                return tokens;
            }
            if (transcription.status == TranscriptionStatus::Error) {
                deleteTranscription(transcriptionId);
                deleteFile(fileId);
                throw std::runtime_error(
                    "[sonioxpp] Transcription failed: " + transcription.error_message);
            }
        }
    }

private:
    // -----------------------------------------------------------------------
    // Internal HTTPS helper — creates a fresh TLS connection per call.
    // -----------------------------------------------------------------------
    std::string httpRequest(
        http::verb         method,
        const std::string& target,
        const std::string& contentType,
        const std::string& requestBody)
    {
        net::io_context ioc;
        ssl::context    sslCtx{ssl::context::tlsv12_client};
        sslCtx.set_default_verify_paths();
        sslCtx.set_verify_mode(ssl::verify_peer);

        tcp::resolver                        resolver{ioc};
        beast::ssl_stream<beast::tcp_stream> tlsStream{ioc, sslCtx};

        auto const resolvedEndpoints = resolver.resolve(
            endpoints::AsyncHost, endpoints::AsyncPort);
        beast::get_lowest_layer(tlsStream).connect(resolvedEndpoints);

        if (!SSL_set_tlsext_host_name(tlsStream.native_handle(), endpoints::AsyncHost))
            throw std::runtime_error("[sonioxpp] Failed to set SNI hostname");

        tlsStream.handshake(ssl::stream_base::client);

        http::request<http::string_body> httpReq{method, target, 11};
        httpReq.set(http::field::host,          endpoints::AsyncHost);
        httpReq.set(http::field::user_agent,    "sonioxpp/1.0");
        httpReq.set(http::field::authorization, "Bearer " + _apiKey);
        if (!contentType.empty())
            httpReq.set(http::field::content_type, contentType);
        httpReq.body() = requestBody;
        httpReq.prepare_payload();

        http::write(tlsStream, httpReq);

        beast::flat_buffer                readBuffer;
        http::response<http::string_body> httpResponse;
        http::read(tlsStream, readBuffer, httpResponse);

        // Graceful TLS shutdown (ignore errors; remote may close first)
        beast::error_code ec;
        tlsStream.shutdown(ec);

        if (httpResponse.result_int() >= 400) {
            throw std::runtime_error(
                "[sonioxpp] HTTP " + std::to_string(httpResponse.result_int()) +
                " on " + target + ": " + httpResponse.body());
        }

        return httpResponse.body();
    }

    std::string _apiKey;
};

// ---------------------------------------------------------------------------
// AsyncClient — public interface (pimpl forwarding)
// ---------------------------------------------------------------------------

AsyncClient::AsyncClient(const std::string& apiKey)
    : impl_(std::make_unique<AsyncClientImpl>(apiKey)) {}

AsyncClient::~AsyncClient() = default;
AsyncClient::AsyncClient(AsyncClient&&)            noexcept = default;
AsyncClient& AsyncClient::operator=(AsyncClient&&) noexcept = default;

std::string AsyncClient::uploadFile(const std::string& filePath) {
    return impl_->uploadFile(filePath);
}
void AsyncClient::deleteFile(const std::string& fileId) {
    impl_->deleteFile(fileId);
}
std::string AsyncClient::createTranscription(const AsyncConfig& config) {
    return impl_->createTranscription(config);
}
AsyncTranscription AsyncClient::getTranscription(const std::string& transcriptionId) {
    return impl_->getTranscription(transcriptionId);
}
std::vector<Token> AsyncClient::getTranscript(const std::string& transcriptionId) {
    return impl_->getTranscript(transcriptionId);
}
void AsyncClient::deleteTranscription(const std::string& transcriptionId) {
    impl_->deleteTranscription(transcriptionId);
}
std::vector<Token> AsyncClient::transcribeFile(
    const std::string& filePath, AsyncConfig config, int pollIntervalMs)
{
    return impl_->transcribeFile(filePath, std::move(config), pollIntervalMs);
}

} // namespace soniox
