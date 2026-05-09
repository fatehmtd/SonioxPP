#pragma once

/// @file curl_http_transport.hpp
/// @brief libcurl-based implementation of `IHttpTransport`.
///
/// `CurlHttpTransport` is the default HTTP backend used by `SttRestClient`,
/// `TtsRestClient`, and `AuthClient` on all platforms.
///
/// Features:
///  - JSON request/response (POST, GET, DELETE)
///  - `multipart/form-data` file uploads (`POST /v1/files`)
///  - Response header capture
///  - TTS trailer capture (`X-Tts-Error-Code`, `X-Tts-Error-Message`)
///  - TLS via the system's OpenSSL (Linux/macOS) or Schannel (Windows) backend
///    bundled with the fetched libcurl

#include "http_transport.hpp"

namespace soniox::transport {

/// Synchronous HTTP transport backed by libcurl.
///
/// Each call to `send` creates, executes, and cleans up a `CURL` easy handle.
/// The class itself is stateless and therefore thread-safe when used with
/// separate instances, or when the caller serialises concurrent calls.
class CurlHttpTransport final : public IHttpTransport {
public:
    /// @copydoc IHttpTransport::send
    HttpResponse send(const HttpRequest& request) override;
};

} // namespace soniox::transport
