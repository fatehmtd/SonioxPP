#pragma once

#include <sonioxpp/transport/http_transport.hpp>

#include <memory>
#include <string>

namespace soniox {

struct TemporaryApiKeyResponse {
    std::string api_key;
    std::string expires_at;
};

struct TemporaryApiKeyRequest {
    std::string usage_type;
    int expires_in_seconds{0};
    std::string client_reference_id;
    bool single_use{false};
    int max_session_duration_seconds{0};
};

class AuthClient {
public:
    explicit AuthClient(
        std::string api_key,
        std::shared_ptr<transport::IHttpTransport> http_transport = nullptr,
        std::string base_url = "https://api.soniox.com");

    std::string createTemporaryApiKey(const TemporaryApiKeyRequest& request);
    TemporaryApiKeyResponse createTemporaryApiKeyTyped(const TemporaryApiKeyRequest& request);

private:
    std::string api_key_;
    std::string base_url_;
    std::shared_ptr<transport::IHttpTransport> http_transport_;
};

} // namespace soniox
