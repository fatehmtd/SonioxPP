#include "async_client_impl.hpp"

#include <mutex>

namespace soniox {

// ---------------------------------------------------------------------------
// One-time libcurl global init — called the first time AsyncClientImpl is
// constructed. curl_global_init is not thread-safe; in practice this runs
// before any user threads start. If your application is multi-threaded at
// startup, call curl_global_init(CURL_GLOBAL_DEFAULT) yourself before
// constructing any AsyncClient.
// ---------------------------------------------------------------------------

AsyncClientImpl::AsyncClientImpl(const std::string& apiKey) : _apiKey(apiKey)
{
    static std::once_flag curlInitFlag;
    std::call_once(curlInitFlag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

// ---------------------------------------------------------------------------
// curlWriteCallback — libcurl write function; appends received bytes to string
// ---------------------------------------------------------------------------

size_t AsyncClientImpl::curlWriteCallback(
    char* ptr, size_t size, size_t nmemb, std::string* out)
{
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// ---------------------------------------------------------------------------
// httpRequest — generic HTTPS helper; one curl handle per call
// ---------------------------------------------------------------------------

std::string AsyncClientImpl::httpRequest(
    const std::string& verb,
    const std::string& target,
    const std::string& contentType,
    const std::string& requestBody)
{
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("[sonioxpp] curl_easy_init failed");

    std::string response;
    std::string url = std::string("https://") + kHost + target;

    curl_easy_setopt(curl, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &response);

    curl_slist* headers = nullptr;
    std::string authHeader = "Authorization: Bearer " + _apiKey;
    std::string uaHeader   = std::string("User-Agent: ") + USER_AGENT;
    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, uaHeader.c_str());
    if (!contentType.empty()) {
        std::string ctHeader = "Content-Type: " + contentType;
        headers = curl_slist_append(headers, ctHeader.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (verb == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST,          1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    requestBody.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(requestBody.size()));
    } else if (verb == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    // GET is the curl default

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        throw std::runtime_error(
            std::string("[sonioxpp] curl: ") + curl_easy_strerror(res));
    if (httpCode >= 400)
        throw std::runtime_error(
            "[sonioxpp] HTTP " + std::to_string(httpCode) +
            " on " + target + ": " + response);

    return response;
}

// ---------------------------------------------------------------------------
// uploadFile — multipart/form-data POST via curl_mime
// ---------------------------------------------------------------------------

std::string AsyncClientImpl::uploadFile(const std::string& filePath)
{
    auto sep = filePath.find_last_of("/\\");
    std::string fileName = (sep != std::string::npos) ? filePath.substr(sep + 1) : filePath;
    spdlog::info("[sonioxpp] Uploading {}", fileName);

    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("[sonioxpp] curl_easy_init failed");

    std::string response;
    std::string url = std::string("https://") + kHost + kFilesPath;

    curl_mime*     mime = curl_mime_init(curl);
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    if (curl_mime_filedata(part, filePath.c_str()) != CURLE_OK) {
        curl_mime_free(mime);
        curl_easy_cleanup(curl);
        throw std::runtime_error("[sonioxpp] Cannot open file: " + filePath);
    }

    curl_easy_setopt(curl, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST,      mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &response);

    curl_slist* headers = nullptr;
    std::string authHeader = "Authorization: Bearer " + _apiKey;
    std::string uaHeader   = std::string("User-Agent: ") + USER_AGENT;
    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, uaHeader.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_mime_free(mime);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        throw std::runtime_error(
            std::string("[sonioxpp] curl: ") + curl_easy_strerror(res));
    if (httpCode >= 400)
        throw std::runtime_error(
            "[sonioxpp] HTTP " + std::to_string(httpCode) + ": " + response);

    json jsonResponse = json::parse(response);
    if (!jsonResponse.contains("id"))
        throw std::runtime_error(
            "[sonioxpp] uploadFile: unexpected response: " + response);

    std::string fileId = jsonResponse["id"].get<std::string>();
    spdlog::info("[sonioxpp] File uploaded, id={}", fileId);
    return fileId;
}

// ---------------------------------------------------------------------------
// deleteFile
// ---------------------------------------------------------------------------

void AsyncClientImpl::deleteFile(const std::string& fileId)
{
    httpRequest("DELETE", std::string(kFilesPath) + "/" + fileId, "", "");
    spdlog::debug("[sonioxpp] Deleted file id={}", fileId);
}

// ---------------------------------------------------------------------------
// createTranscription
// ---------------------------------------------------------------------------

std::string AsyncClientImpl::createTranscription(const AsyncConfig& config)
{
    if (config.audio_url.empty() && config.file_id.empty())
        throw std::runtime_error(
            "[sonioxpp] createTranscription: set audio_url or file_id in AsyncConfig");

    json jsonContext;
    jsonContext["general"]      = config.context.general;
    jsonContext["text_context"] = config.context.text;
    jsonContext["terms"]        = config.context.terms;

    json jsonReq;
    jsonReq["model"]                          = config.model;
    jsonReq["language_hints"]                 = config.language_hints;
    jsonReq["enable_language_identification"] = config.enable_language_identification;
    jsonReq["enable_speaker_diarization"]     = config.enable_speaker_diarization;
    jsonReq["context"]                        = jsonContext;

    if (!config.audio_url.empty())
        jsonReq["audio_url"] = config.audio_url;
    else
        jsonReq["file_id"] = config.file_id;

    if (!config.translation.type.empty()) {
        json jsonTranslation;
        jsonTranslation["type"] = config.translation.type;
        if (!config.translation.language_a.empty())
            jsonTranslation["language_a"] = config.translation.language_a;
        if (!config.translation.language_b.empty())
            jsonTranslation["language_b"] = config.translation.language_b;
        jsonReq["translation"] = jsonTranslation;
    }

    std::string responseBody = httpRequest(
        "POST", kTranscriptPath, "application/json", jsonReq.dump());

    json jsonResponse = json::parse(responseBody);
    if (!jsonResponse.contains("id"))
        throw std::runtime_error(
            "[sonioxpp] createTranscription: unexpected response: " + responseBody);

    std::string transcriptionId = jsonResponse["id"].get<std::string>();
    spdlog::info("[sonioxpp] Transcription created, id={}", transcriptionId);
    return transcriptionId;
}

// ---------------------------------------------------------------------------
// getTranscription
// ---------------------------------------------------------------------------

AsyncTranscription AsyncClientImpl::getTranscription(const std::string& transcriptionId)
{
    std::string responseBody = httpRequest(
        "GET",
        std::string(kTranscriptPath) + "/" + transcriptionId,
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

// ---------------------------------------------------------------------------
// getTranscript
// ---------------------------------------------------------------------------

std::vector<Token> AsyncClientImpl::getTranscript(const std::string& transcriptionId)
{
    std::string responseBody = httpRequest(
        "GET",
        std::string(kTranscriptPath) + "/" + transcriptionId + "/transcript",
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

// ---------------------------------------------------------------------------
// deleteTranscription
// ---------------------------------------------------------------------------

void AsyncClientImpl::deleteTranscription(const std::string& transcriptionId)
{
    httpRequest("DELETE",
                std::string(kTranscriptPath) + "/" + transcriptionId,
                "", "");
    spdlog::debug("[sonioxpp] Deleted transcription id={}", transcriptionId);
}

// ---------------------------------------------------------------------------
// transcribeFile — convenience: upload → create → poll → fetch → cleanup
// ---------------------------------------------------------------------------

std::vector<Token> AsyncClientImpl::transcribeFile(
    const std::string& filePath, AsyncConfig config, int pollIntervalMs)
{
    std::string fileId = uploadFile(filePath);
    config.file_id = fileId;
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
