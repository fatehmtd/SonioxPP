#pragma once

#include "types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace soniox {

class AsyncClientImpl;

/// Async (REST) STT client. Not copyable; movable.
class AsyncClient {
public:
    explicit AsyncClient(const std::string& api_key);
    ~AsyncClient();

    AsyncClient(const AsyncClient&)            = delete;
    AsyncClient& operator=(const AsyncClient&) = delete;
    AsyncClient(AsyncClient&&)            noexcept;
    AsyncClient& operator=(AsyncClient&&) noexcept;

    // -----------------------------------------------------------------------
    // File management
    // -----------------------------------------------------------------------

    /* Returns file ID to pass to createTranscription().
       Throws std::runtime_error on HTTP or I/O failure. */
    std::string uploadFile(const std::string& file_path);

    void deleteFile(const std::string& file_id);

    // -----------------------------------------------------------------------
    // Transcription lifecycle
    // -----------------------------------------------------------------------

    /* Set either config.audio_url or config.file_id before calling.
       Returns transcription job ID.
       Throws std::runtime_error on HTTP failure or missing audio source. */
    std::string createTranscription(const AsyncConfig& config);

    AsyncTranscription getTranscription(const std::string& id);

    /// @throws std::runtime_error if the job is not yet completed.
    std::vector<Token> getTranscript(const std::string& id);

    void deleteTranscription(const std::string& id);

    // -----------------------------------------------------------------------
    // Convenience
    // -----------------------------------------------------------------------

    /// Upload, transcribe, poll until done, retrieve tokens, then clean up.
    std::vector<Token> transcribeFile(
        const std::string& file_path,
        AsyncConfig        config,
        int                poll_interval_ms = 1000);

private:
    std::unique_ptr<AsyncClientImpl> _impl;
};

} // namespace soniox
