#include <sonioxpp/transport/curl_http_transport.hpp>

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace soniox::transport {
namespace {

struct CurlResponseContext {
    HttpResponse response;
};

std::string toLowerCopy(const std::string& value)
{
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string trimCopy(const std::string& value)
{
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(start, end - start);
}

size_t writeBodyCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    const auto bytes = size * nmemb;
    auto* ctx = static_cast<CurlResponseContext*>(userdata);
    auto* body = &ctx->response.body;
    const auto* begin = reinterpret_cast<const std::uint8_t*>(ptr);
    body->insert(body->end(), begin, begin + bytes);
    return bytes;
}

size_t writeHeaderCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    const auto bytes = size * nmemb;
    std::string line(ptr, bytes);
    auto* ctx = static_cast<CurlResponseContext*>(userdata);

    const auto colon = line.find(':');
    if (colon == std::string::npos) {
        return bytes;
    }

    const std::string key = toLowerCopy(trimCopy(line.substr(0, colon)));
    const std::string value = trimCopy(line.substr(colon + 1));

    if (!key.empty()) {
        ctx->response.headers[key] = value;
        if (key == "x-tts-error-code" || key == "x-tts-error-message") {
            ctx->response.trailers[key] = value;
        }
    }

    return bytes;
}

curl_mime* buildMime(CURL* curl, const HttpRequest& request)
{
    if (request.multipart_files.empty()) {
        return nullptr;
    }

    curl_mime* mime = curl_mime_init(curl);
    for (const auto& file : request.multipart_files) {
        curl_mimepart* part = curl_mime_addpart(mime);
        curl_mime_name(part, file.field_name.c_str());
        if (!file.content_type.empty()) {
            curl_mime_type(part, file.content_type.c_str());
        }
        const CURLcode rc = curl_mime_filedata(part, file.file_path.c_str());
        if (rc != CURLE_OK) {
            curl_mime_free(mime);
            throw std::runtime_error("[sonioxpp] curl_mime_filedata failed for " + file.file_path);
        }
    }

    return mime;
}

} // namespace

HttpResponse CurlHttpTransport::send(const HttpRequest& request)
{
    static const int curlGlobalInitResult = [] {
        return curl_global_init(CURL_GLOBAL_DEFAULT);
    }();

    if (curlGlobalInitResult != CURLE_OK) {
        throw std::runtime_error("[sonioxpp] curl_global_init failed");
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        throw std::runtime_error("[sonioxpp] curl_easy_init failed");
    }

    CurlResponseContext ctx;

    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBodyCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeHeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ctx);

    if (request.timeout_ms > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, request.timeout_ms);
    }

    switch (request.method) {
    case HttpMethod::Get:
        break;
    case HttpMethod::Post:
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (!request.body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
        }
        break;
    case HttpMethod::Delete:
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        break;
    }

    curl_mime* mime = nullptr;
    if (!request.multipart_files.empty()) {
        mime = buildMime(curl, request);
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    }

    curl_slist* headerList = nullptr;
    for (const auto& [key, value] : request.headers) {
        const std::string item = key + ": " + value;
        headerList = curl_slist_append(headerList, item.c_str());
    }
    if (!request.content_type.empty()) {
        const std::string contentType = "Content-Type: " + request.content_type;
        headerList = curl_slist_append(headerList, contentType.c_str());
    }
    if (headerList != nullptr) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }

    const CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ctx.response.status_code);

    if (mime != nullptr) {
        curl_mime_free(mime);
    }
    if (headerList != nullptr) {
        curl_slist_free_all(headerList);
    }
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        throw std::runtime_error(std::string("[sonioxpp] curl perform failed: ") + curl_easy_strerror(rc));
    }

    return ctx.response;
}

} // namespace soniox::transport
