#pragma once

/// @file http_transport.hpp
/// @brief HTTP transport interface and supporting data types.
///
/// `IHttpTransport` is the single extension point for HTTP I/O in SonioxPP.
/// The default implementation is `CurlHttpTransport` (libcurl).  Swap it out
/// by passing a custom `std::shared_ptr<IHttpTransport>` to any REST client
/// constructor.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace soniox::transport {

/// HTTP verb used in `HttpRequest::method`.
enum class HttpMethod {
    Get,
    Post,
    Delete
};

/// Describes a single part in a `multipart/form-data` upload.
/// Used by `SttRestClient::uploadFile` to POST audio files.
struct MultipartFile {
    std::string field_name;   ///< Form field name (e.g. `"file"`)
    std::string file_path;    ///< Absolute or relative path to the file on disk
    std::string content_type; ///< MIME type (empty = auto-detect from extension)
};

/// An HTTP request to be dispatched by `IHttpTransport::send`.
struct HttpRequest {
    HttpMethod  method{HttpMethod::Get}; ///< HTTP verb
    std::string url;                     ///< Fully-qualified URL (https://...)
    std::map<std::string, std::string> headers; ///< Additional request headers
    std::string content_type;            ///< `Content-Type` header value (set automatically when `body` is non-empty)
    std::string body;                    ///< JSON or other text body for POST requests
    std::vector<MultipartFile> multipart_files; ///< Non-empty for multipart uploads; mutually exclusive with `body`
    long timeout_ms{0};                  ///< Request timeout in milliseconds; 0 = transport default
};

/// An HTTP response returned by `IHttpTransport::send`.
struct HttpResponse {
    long status_code{0};                         ///< HTTP status code (e.g. 200, 201, 400)
    std::map<std::string, std::string> headers;  ///< Response headers (lower-cased keys)
    std::map<std::string, std::string> trailers; ///< HTTP/1.1 trailers — used by the TTS endpoint to
                                                 ///< propagate `X-Tts-Error-Code` / `X-Tts-Error-Message`
                                                 ///< when an error occurs mid-stream after headers are sent
    std::vector<std::uint8_t> body;              ///< Raw response body bytes
};

/// Synchronous HTTP transport interface.
///
/// Implementations must be thread-compatible but are not required to be
/// thread-safe — callers should use separate instances (or external locking)
/// if concurrent requests are needed.
class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;

    /// Execute a synchronous HTTP request and return the response.
    ///
    /// @param request  Fully populated request descriptor.
    /// @return         Response including status, headers, trailers, and body.
    /// @throws std::runtime_error (or a subclass) on unrecoverable I/O or TLS errors.
    ///         HTTP-level errors (status >= 400) are **not** thrown here —
    ///         callers inspect `HttpResponse::status_code` and throw
    ///         `SonioxApiException` accordingly.
    virtual HttpResponse send(const HttpRequest& request) = 0;
};

} // namespace soniox::transport
