#pragma once

#include "http_transport.hpp"

namespace soniox::transport {

/// Stateless libcurl-backed HTTP transport. Thread-safe with separate instances.
class CurlHttpTransport final : public IHttpTransport {
public:
    /// @copydoc IHttpTransport::send
    HttpResponse send(const HttpRequest& request) override;
};

} // namespace soniox::transport
