#pragma once

#include "http_transport.hpp"

namespace soniox::transport {

/// Stateless libcurl-backed HTTP transport. Thread-safe with separate instances.
class CurlHttpTransport final : public IHttpTransport {
public:
    /* caFilePath: CA bundle (PEM) consulted for the TLS handshake. mbedTLS (our TLS
       backend everywhere except Windows) ships with no built-in trust anchors, so on
       platforms where the OS trust store isn't visible to it (e.g. Android) this must
       be set explicitly; leave empty to use the platform default. */
    explicit CurlHttpTransport(std::string caFilePath = {});

    /// @copydoc IHttpTransport::send
    HttpResponse send(const HttpRequest& request) override;

private:
    std::string _caFilePath;
};

} // namespace soniox::transport
