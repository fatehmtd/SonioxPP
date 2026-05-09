#pragma once

/// @file auth_client.hpp
/// @brief Client for the Soniox Auth API — creating temporary API keys.
///
/// ### Endpoint
/// `POST https://api.soniox.com/v1/auth/temporary-api-key`
///
/// Temporary keys are short-lived credentials intended for client-side use
/// (e.g., browsers, mobile apps) so that your master API key is never exposed.
/// Each key is scoped to a single usage type and can optionally be single-use.
///
/// @see https://soniox.com/docs/temporary-api-keys

#include <sonioxpp/transport/http_transport.hpp>

#include <memory>
#include <string>

namespace soniox {

/// A temporary API key returned by the Soniox Auth API.
struct TemporaryApiKeyResponse {
    std::string api_key;     ///< The short-lived API key to pass to a client-side WebSocket session
    std::string expires_at;  ///< ISO 8601 timestamp when the key expires
};

/// Request body for `POST /v1/auth/temporary-api-key`.
///
/// ### Typical usage — single-use real-time STT key
/// @code
///   soniox::AuthClient auth("YOUR_MASTER_KEY");
///
///   auto tmp = auth.createTemporaryApiKeyTyped({
///       .usage_type         = soniox::auth::temporary_api_key_usage::transcribe_websocket,
///       .expires_in_seconds = 900,     // 15 minutes
///       .single_use         = true,
///   });
///   // Hand tmp.api_key to a client-side WebSocket session.
/// @endcode
struct TemporaryApiKeyRequest {
    /// Intended use case. Controls which endpoint the key is authorised for.
    /// Use `soniox::auth::temporary_api_key_usage::transcribe_websocket` for STT
    /// or `soniox::auth::temporary_api_key_usage::tts_rt` for TTS.
    std::string usage_type;

    /// Key validity in seconds (required; maximum 3 600).
    int expires_in_seconds{0};

    /// Opaque client-side tracking tag attached to sessions that use this key (max 256 chars).
    std::string client_reference_id;

    /// When `true`, the key is invalidated after the first successful session.
    bool single_use{false};

    /// Maximum duration in seconds for any single session that uses this key.
    /// 0 = no additional cap beyond `expires_in_seconds`.
    int max_session_duration_seconds{0};
};

/// Client for creating temporary Soniox API keys.
///
/// Uses the master API key (passed to the constructor) to mint short-lived
/// keys that can be safely distributed to end-users for real-time sessions.
class AuthClient {
public:
    /// @param api_key        Master Soniox API key (Bearer token).
    /// @param http_transport Custom HTTP backend; nullptr = use `CurlHttpTransport`.
    /// @param base_url       API base URL (override for regional endpoints or testing).
    explicit AuthClient(
        std::string api_key,
        std::shared_ptr<transport::IHttpTransport> http_transport = nullptr,
        std::string base_url = "https://api.soniox.com");

    /// Create a temporary API key. Returns the raw JSON response body.
    /// @throws SonioxApiException on HTTP >= 400.
    std::string createTemporaryApiKey(const TemporaryApiKeyRequest& request);

    /// Create a temporary API key. Returns a typed `TemporaryApiKeyResponse`.
    /// @throws SonioxApiException on HTTP >= 400.
    TemporaryApiKeyResponse createTemporaryApiKeyTyped(const TemporaryApiKeyRequest& request);

private:
    std::string _apiKey;
    std::string _baseUrl;
    std::shared_ptr<transport::IHttpTransport> _httpTransport;
};

} // namespace soniox
