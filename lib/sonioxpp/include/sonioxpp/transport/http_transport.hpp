#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace soniox::transport {

enum class HttpMethod {
    Get,
    Post,
    Delete
};

struct MultipartFile {
    std::string field_name;
    std::string file_path;
    std::string content_type;
};

struct HttpRequest {
    HttpMethod method{HttpMethod::Get};
    std::string url;
    std::map<std::string, std::string> headers;
    std::string content_type;
    std::string body;
    std::vector<MultipartFile> multipart_files;
    long timeout_ms{0};
};

struct HttpResponse {
    long status_code{0};
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> trailers;
    std::vector<std::uint8_t> body;
};

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;

    virtual HttpResponse send(const HttpRequest& request) = 0;
};

} // namespace soniox::transport
