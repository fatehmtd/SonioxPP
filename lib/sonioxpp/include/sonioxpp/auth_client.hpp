#pragma once

#include <sonioxpp/transport/http_transport.hpp>

#include <memory>
#include <string>

namespace soniox {

struct TemporaryApiKeyResponse {
    std::string api_key;
    std::string expires_at;
};

/// Request body for `POST /v1/auth/temporary-api-key`.
struct TemporaryApiKeyRequest {
    std::string usage_type;            ///< see soniox::auth::temporary_api_key_usage
    int         expires_in_seconds{0}; ///< max 3600
    std::string client_reference_id;
    bool        single_use{false};
    int         max_session_duration_seconds{0};
};

/// Mints short-lived API keys for client-side use via POST /v1/auth/temporary-api-key.
class AuthClient {
public:
    explicit AuthClient(
        std::string api_key,
        std::shared_ptr<transport::IHttpTransport> http_transport = nullptr,
        std::string base_url = "https://api.soniox.com");

    /// @throws SonioxApiException on HTTP >= 400.
    std::string createTemporaryApiKey(const TemporaryApiKeyRequest& request);

    /// @throws SonioxApiException on HTTP >= 400.
    TemporaryApiKeyResponse createTemporaryApiKeyTyped(const TemporaryApiKeyRequest& request);

private:
    std::string _apiKey;
    std::string _baseUrl;
    std::shared_ptr<transport::IHttpTransport> _httpTransport;
};

} // namespace soniox
