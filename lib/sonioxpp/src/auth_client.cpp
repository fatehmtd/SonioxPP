#include <sonioxpp/auth_client.hpp>
#include <sonioxpp/transport/curl_http_transport.hpp>
#include <sonioxpp/types.hpp>

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace soniox {
namespace {

using json = nlohmann::json;

void throwIfError(const transport::HttpResponse& response, const std::string& operation)
{
    if (response.status_code >= 400) {
        const std::string body(response.body.begin(), response.body.end());
        ApiErrorDetail detail;
        detail.status_code = static_cast<int>(response.status_code);
        detail.raw_body = body;

        const auto payload = json::parse(body, nullptr, false);
        if (!payload.is_discarded()) {
            detail.status_code = payload.value("status_code", detail.status_code);
            if (payload.contains("error_type") && payload["error_type"].is_string()) {
                detail.error_type = payload["error_type"].get<std::string>();
            }
            if (payload.contains("message") && payload["message"].is_string()) {
                detail.message = payload["message"].get<std::string>();
            }
            if (payload.contains("request_id") && payload["request_id"].is_string()) {
                detail.request_id = payload["request_id"].get<std::string>();
            }

            if (payload.contains("validation_errors") && payload["validation_errors"].is_array()) {
                for (const auto& ve : payload["validation_errors"]) {
                    ApiValidationError validation;
                    if (ve.contains("error_type") && ve["error_type"].is_string()) {
                        validation.error_type = ve["error_type"].get<std::string>();
                    }
                    if (ve.contains("location") && ve["location"].is_string()) {
                        validation.location = ve["location"].get<std::string>();
                    }
                    if (ve.contains("message") && ve["message"].is_string()) {
                        validation.message = ve["message"].get<std::string>();
                    }
                    detail.validation_errors.push_back(std::move(validation));
                }
            }
        }

        throw SonioxApiException(operation, std::move(detail));
    }
}

} // namespace

AuthClient::AuthClient(
    std::string api_key,
    std::shared_ptr<transport::IHttpTransport> http_transport,
    std::string base_url)
    : _apiKey(std::move(api_key)),
      _baseUrl(std::move(base_url)),
      _httpTransport(std::move(http_transport))
{
    if (!_httpTransport) {
        _httpTransport = std::make_shared<transport::CurlHttpTransport>();
    }
}

std::string AuthClient::createTemporaryApiKey(const TemporaryApiKeyRequest& request)
{
    json body;
    body["usage_type"] = request.usage_type;
    body["expires_in_seconds"] = request.expires_in_seconds;

    if (!request.client_reference_id.empty()) {
        body["client_reference_id"] = request.client_reference_id;
    }
    if (request.single_use) {
        body["single_use"] = true;
    }
    if (request.max_session_duration_seconds > 0) {
        body["max_session_duration_seconds"] = request.max_session_duration_seconds;
    }

    transport::HttpRequest http_request;
    http_request.method = transport::HttpMethod::Post;
    http_request.url = _baseUrl + "/v1/auth/temporary-api-key";
    http_request.content_type = "application/json";
    http_request.body = body.dump();
    http_request.headers["Authorization"] = "Bearer " + _apiKey;
    http_request.headers["User-Agent"] = soniox::USER_AGENT;

    const auto response = _httpTransport->send(http_request);
    throwIfError(response, "create temporary API key");

    return std::string(response.body.begin(), response.body.end());
}

TemporaryApiKeyResponse AuthClient::createTemporaryApiKeyTyped(const TemporaryApiKeyRequest& request)
{
    const auto payload = json::parse(createTemporaryApiKey(request));

    TemporaryApiKeyResponse response;
    if (payload.contains("api_key") && payload["api_key"].is_string()) {
        response.api_key = payload["api_key"].get<std::string>();
    }
    if (payload.contains("expires_at") && payload["expires_at"].is_string()) {
        response.expires_at = payload["expires_at"].get<std::string>();
    }

    return response;
}

} // namespace soniox
