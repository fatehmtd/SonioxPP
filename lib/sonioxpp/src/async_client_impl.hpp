#pragma once

#include <sonioxpp/async_client.hpp>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace soniox {

using json = nlohmann::json;

class AsyncClientImpl {
public:
    explicit AsyncClientImpl(const std::string& apiKey);

    std::string        uploadFile(const std::string& filePath);
    void               deleteFile(const std::string& fileId);
    std::string        createTranscription(const AsyncConfig& config);
    AsyncTranscription getTranscription(const std::string& transcriptionId);
    std::vector<Token> getTranscript(const std::string& transcriptionId);
    void               deleteTranscription(const std::string& transcriptionId);
    std::vector<Token> transcribeFile(const std::string& filePath,
                                      AsyncConfig config, int pollIntervalMs);

private:
    std::string httpRequest(const std::string& verb,
                            const std::string& target,
                            const std::string& contentType,
                            const std::string& requestBody);

    static size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb, std::string* out);

    static constexpr const char* kHost            = "api.soniox.com";
    static constexpr const char* kFilesPath       = "/v1/files";
    static constexpr const char* kTranscriptPath  = "/v1/transcriptions";

    std::string _apiKey;
};

} // namespace soniox
