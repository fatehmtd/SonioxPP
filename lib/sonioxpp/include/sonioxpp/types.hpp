#pragma once

#include <sonioxpp/api_constants.hpp>

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace soniox {

constexpr const char* USER_AGENT = "sonioxpp/1.0";
constexpr const char* SONIOX_BASE_URL = "https://api.soniox.com";

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

/// A single transcription token (word, punctuation, or silence).
struct Token {
    std::string text;               ///< may be empty for silence
    bool        is_final{false};    ///< true once Soniox will not revise this token
    int         speaker{0};         ///< numeric speaker label (API sends a string, e.g. "1")
    std::string language;
    std::string source_language;    ///< original language of a translated token
    std::string translation_status;
    int         start_ms{0};        ///< omitted by the API for translation tokens
    int         end_ms{0};          ///< omitted by the API for translation tokens
    int         duration_ms{0};     ///< derived: end_ms - start_ms
    double      confidence{0.0};    ///< 0.0–1.0
};

/// Domain-specific context to improve transcription accuracy.
struct Context {
    std::unordered_map<std::string, std::string> general;
    std::string              text;
    std::vector<std::string> terms;
    std::unordered_map<std::string, std::string> translation_terms;
};

/// Translation configuration (leave type empty to disable).
struct Translation {
    std::string type;            ///< "one_way" or "two_way"
    std::string target_language; ///< one_way only
    std::string language_a;      ///< two_way only (also accepted as one_way target for back-compat)
    std::string language_b;      ///< two_way only
};

/// Cursor-based pagination for list endpoints.
struct PaginationQuery {
    int         limit{0};  ///< 1–1000; 0 = server default of 1000
    std::string cursor;    ///< opaque cursor from next_page_cursor; empty = first page
};

// ---------------------------------------------------------------------------
// Realtime config
// ---------------------------------------------------------------------------

/// Configuration sent at the start of a real-time WebSocket session.
struct RealtimeConfig {
    std::string api_key;
    std::string model{stt::models::realtime_v5};
    std::string audio_format{stt::audio_formats::auto_detect};
    int         sample_rate{0};    ///< required when audio_format is pcm_s16le/pcm_s16be
    int         num_channels{0};   ///< required when audio_format is pcm_s16le/pcm_s16be
    std::vector<std::string> language_hints;
    bool language_hints_strict{false};
    bool enable_language_identification{false};
    bool enable_speaker_diarization{false};
    bool enable_endpoint_detection{false};
    int    max_endpoint_delay_ms{0};             ///< 500–3000 ms; 0 = server default of 2000
    double endpoint_sensitivity{0.0};            ///< -1.0–1.0; 0.0 = server default
    int    endpoint_latency_adjustment_level{0}; ///< 0–3; 0 = server default
    Context     context;
    Translation translation;
    std::string client_reference_id; ///< custom session identifier, max 256 chars
};

// ---------------------------------------------------------------------------
// Async config & result
// ---------------------------------------------------------------------------

/// Configuration for an async (REST) transcription job.
struct AsyncConfig {
    std::string api_key;
    std::string model{stt::models::async_v5};
    std::vector<std::string> language_hints;
    bool language_hints_strict{false};
    bool enable_language_identification{false};
    bool enable_speaker_diarization{false};
    Context     context;
    Translation translation;

    // Set exactly one audio source:
    std::string audio_url;
    std::string file_id;

    std::string webhook_url;              ///< notified on completion or failure
    std::string webhook_auth_header_name;
    std::string webhook_auth_header_value;

    std::string client_reference_id; ///< custom tracking identifier, max 256 chars
};

/// Status of an async transcription job.
enum class TranscriptionStatus { Pending, Running, Completed, Error };

/// Metadata about an async transcription job returned by polling.
struct AsyncTranscription {
    std::string          id;
    TranscriptionStatus  status{TranscriptionStatus::Pending};
    std::string          error_message;
};

// ---------------------------------------------------------------------------
// Callback signatures
// ---------------------------------------------------------------------------

using OnTokensCallback  = std::function<void(const std::vector<Token>&, bool /*has_final*/)>;
using OnFinishedCallback = std::function<void()>;

struct RealtimeError {
    int         error_code{0};
    std::string error_message;
};

using OnErrorCallback = std::function<void(const RealtimeError&)>;

struct ApiValidationError {
    std::string error_type;
    std::string location;
    std::string message;
};

struct ApiErrorDetail {
    int status_code{0};
    std::string error_type;
    std::string message;
    std::string request_id;
    std::vector<ApiValidationError> validation_errors;
    std::string raw_body;
};

/// Thrown for Soniox REST API errors (HTTP >= 400).
class SonioxApiException : public std::runtime_error {
public:
    SonioxApiException(
        std::string operation,
        ApiErrorDetail detail)
        : std::runtime_error(makeWhat(operation, detail)),
          operation_(std::move(operation)),
          detail_(std::move(detail))
    {
    }

    const std::string& operation() const noexcept { return operation_; }
    const ApiErrorDetail& detail() const noexcept { return detail_; }

private:
    static std::string makeWhat(const std::string& operation, const ApiErrorDetail& detail)
    {
        std::string out = "[sonioxpp] " + operation + " failed with status " + std::to_string(detail.status_code);
        if (!detail.error_type.empty()) {
            out += ", type=" + detail.error_type;
        }
        if (!detail.message.empty()) {
            out += ", message=" + detail.message;
        }
        if (!detail.request_id.empty()) {
            out += ", request_id=" + detail.request_id;
        }
        return out;
    }

    std::string operation_;
    ApiErrorDetail detail_;
};

} // namespace soniox
