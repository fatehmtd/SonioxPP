#pragma once

#include "http_transport.hpp"

namespace soniox::transport {

class CurlHttpTransport final : public IHttpTransport {
public:
    HttpResponse send(const HttpRequest& request) override;
};

} // namespace soniox::transport
