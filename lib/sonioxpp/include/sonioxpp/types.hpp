#pragma once

#include <sonioxpp/api_constants.hpp>

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

/// @file types.hpp
/// @brief Common types, structures, and callback signatures for the soniox namespace.

namespace soniox {

constexpr const char* USER_AGENT = "sonioxpp/1.0";

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

/// A single transcription token (word, punctuation, or silence).
struct Token {
    std::string text;               ///< Token text (may be empty for silence)
    bool        is_final{false};    ///< True once Soniox will not revise this token
    int         speaker{0};        ///< Speaker ID (0-based); populated with diarization
    std::string language;           ///< BCP-47 code of detected language; populated with lang-ID
    std::string translation_status; ///< e.g. "translated"; populated with translation enabled
    int         start_ms{0};       ///< Start time in ms (async API only)
    int         duration_ms{0};    ///< Duration in ms (async API only)
};

/// Domain-specific context to improve transcription accuracy.
struct Context {
    std::unordered_map<std::string, std::string> general;      ///< Domain hint (e.g. "medical", "legal", "general")
    std::string              text; ///< Free-text context preamble
    std::vector<std::string> terms;        ///< Custom vocabulary/proper-noun list
    std::unordered_map<std::string, std::string> translation_terms; ///< Custom translation vocabulary (key=source term, value=desired translation)
};

/// Translation configuration (leave mode empty to disable).
struct Translation {
    std::string type;            ///< "one_way" or "two_way"
    std::string language_a; ///< Target BCP-47 code for one_way translation
    std::string language_b; ///< Source BCP-47 code for two_way translation
};

// ---------------------------------------------------------------------------
// Realtime config
// ---------------------------------------------------------------------------

/// Configuration sent at the start of a real-time WebSocket session.
struct RealtimeConfig {
    std::string api_key;                         ///< Soniox API key (required)
    std::string model{stt::models::realtime_v4}; ///< Transcription model
    std::string audio_format{stt::audio_formats::auto_detect}; ///< "auto", "pcm_s16le", etc.
    int         sample_rate{0};                  ///< PCM sample rate in Hz (required when audio_format is "pcm_s16le")
    int         num_channels{0};                 ///< Number of audio channels (required when audio_format is "pcm_s16le")
    std::vector<std::string> language_hints;     ///< e.g. {"en", "es"}
    bool enable_language_identification{false};  ///< Detect language per token
    bool enable_speaker_diarization{false};      ///< Assign speaker IDs
    bool enable_endpoint_detection{false};       ///< Detect natural speech pauses
    Context     context;                         ///< Domain context
    Translation translation;                     ///< Translation settings (empty = disabled)
};

// ---------------------------------------------------------------------------
// Async config & result
// ---------------------------------------------------------------------------

/// Configuration for an async (REST) transcription job.
struct AsyncConfig {
    std::string api_key;                         ///< Soniox API key (required)
    std::string model{stt::models::async_v4}; ///< Transcription model
    std::vector<std::string> language_hints;     ///< e.g. {"en"}
    bool enable_language_identification{false};  ///< Detect language per token
    bool enable_speaker_diarization{false};      ///< Assign speaker IDs
    Context     context;                         ///< Domain context
    Translation translation;                     ///< Translation settings (empty = disabled)

    /// Set one of these to specify the audio source:
    std::string audio_url;  ///< Publicly accessible URL
    std::string file_id;    ///< File ID returned by AsyncClient::uploadFile()
};

/// Status of an async transcription job.
enum class TranscriptionStatus { Pending, Running, Completed, Error };

/// Metadata about an async transcription job returned by polling.
struct AsyncTranscription {
    std::string          id;
    TranscriptionStatus  status{TranscriptionStatus::Pending};
    std::string          error_message; ///< Non-empty when status == Error
};

// ---------------------------------------------------------------------------
// Callback signatures
// ---------------------------------------------------------------------------

/// Called when the server sends a token update.
/// @param tokens     All tokens in the current response batch.
/// @param has_final  True if at least one token in the batch is final.
using OnTokensCallback = std::function<void(const std::vector<Token>&, bool /*has_final*/)>;

/// Called once the session is finished (server sent finished:true).
using OnFinishedCallback = std::function<void()>;

/// Error received from the Soniox server or from a network/protocol failure.
struct RealtimeError {
    int         error_code{0};    ///< Numeric error code from the server (e.g. 400, 503); 0 for network errors
    std::string error_message;    ///< Human-readable error description
};

/// Called when the server sends an error response or a network/protocol failure occurs.
using OnErrorCallback = std::function<void(const RealtimeError&)>;

/// Structured API error details parsed from Soniox REST responses.
struct ApiValidationError {
    std::string error_type;
    std::string location;
    std::string message;
};

/// Structured API error details parsed from Soniox REST responses.
struct ApiErrorDetail {
    int status_code{0};
    std::string error_type;
    std::string message;
    std::string request_id;
    std::vector<ApiValidationError> validation_errors;
    std::string raw_body;
};

/// Exception thrown for Soniox REST API errors (HTTP >= 400) with parsed metadata.
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
