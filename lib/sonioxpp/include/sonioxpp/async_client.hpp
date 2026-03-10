#pragma once

#include "types.hpp"

#include <memory>
#include <string>
#include <vector>

/// @file async_client.hpp
/// @brief Async (REST) STT client for the Soniox API.

namespace soniox {

class AsyncClientImpl; ///< Forward-declared pimpl

/**
 * @brief Uploads audio and transcribes it asynchronously via the Soniox REST API.
 *
 * ### Base URL
 * `https://api.soniox.com`
 *
 * ### Relevant endpoints
 * | Method   | Path                                     | Purpose                     |
 * |----------|------------------------------------------|-----------------------------|
 * | POST     | /v1/files                                | Upload a local audio file   |
 * | DELETE   | /v1/files/{id}                           | Delete an uploaded file     |
 * | POST     | /v1/transcriptions                       | Create a transcription job  |
 * | GET      | /v1/transcriptions/{id}                  | Poll job status             |
 * | GET      | /v1/transcriptions/{id}/transcript       | Retrieve completed tokens   |
 * | DELETE   | /v1/transcriptions/{id}                  | Delete a transcription job  |
 *
 * ### Typical usage — single-call convenience
 * @code
 *   soniox::AsyncConfig cfg;
 *   cfg.api_key       = "YOUR_KEY";
 *   cfg.language_hints = {"en"};
 *   cfg.enable_speaker_diarization = true;
 *
 *   soniox::AsyncClient client(cfg.api_key);
 *   auto tokens = client.transcribeFile("/path/to/audio.wav", cfg);
 * @endcode
 *
 * ### Step-by-step usage
 * @code
 *   soniox::AsyncClient client("YOUR_KEY");
 *   std::string file_id = client.uploadFile("/path/to/audio.wav");
 *   cfg.file_id = file_id;
 *   std::string tx_id  = client.createTranscription(cfg);
 *
 *   soniox::AsyncTranscription tx;
 *   do {
 *       std::this_thread::sleep_for(std::chrono::seconds(1));
 *       tx = client.getTranscription(tx_id);
 *   } while (tx.status == soniox::TranscriptionStatus::Pending ||
 *            tx.status == soniox::TranscriptionStatus::Running);
 *
 *   auto tokens = client.getTranscript(tx_id);
 *   client.deleteTranscription(tx_id);
 *   client.deleteFile(file_id);
 * @endcode
 *
 * @note AsyncClient is not copyable; it is movable.
 */
class AsyncClient {
public:
    /// @param api_key  Your Soniox API key (Bearer token).
    explicit AsyncClient(const std::string& api_key);
    ~AsyncClient();

    AsyncClient(const AsyncClient&)            = delete;
    AsyncClient& operator=(const AsyncClient&) = delete;
    AsyncClient(AsyncClient&&)            noexcept;
    AsyncClient& operator=(AsyncClient&&) noexcept;

    // -----------------------------------------------------------------------
    // File management
    // -----------------------------------------------------------------------

    /// Upload a local audio file.
    /// @param file_path  Absolute or relative path to the audio file.
    /// @return           File ID to pass to createTranscription().
    /// @throws std::runtime_error on HTTP or I/O failure.
    std::string uploadFile(const std::string& file_path);

    /// Delete a previously uploaded file.
    /// @param file_id  ID returned by uploadFile().
    void deleteFile(const std::string& file_id);

    // -----------------------------------------------------------------------
    // Transcription lifecycle
    // -----------------------------------------------------------------------

    /// Create an async transcription job.
    /// Set either config.audio_url or config.file_id before calling.
    /// @return  Transcription job ID.
    /// @throws std::runtime_error on HTTP failure or missing audio source.
    std::string createTranscription(const AsyncConfig& config);

    /// Poll the current status of a transcription job.
    /// @return  AsyncTranscription with id, status, and error_message fields.
    AsyncTranscription getTranscription(const std::string& id);

    /// Retrieve the completed transcript.
    /// @return  Vector of Token objects (start_ms / duration_ms are populated).
    /// @throws std::runtime_error if the job is not yet completed, or on HTTP failure.
    std::vector<Token> getTranscript(const std::string& id);

    /// Delete a transcription job.
    void deleteTranscription(const std::string& id);

    // -----------------------------------------------------------------------
    // Convenience
    // -----------------------------------------------------------------------

    /**
     * @brief Upload, transcribe, poll until done, return tokens, then clean up.
     *
     * Handles the entire lifecycle:
     *  1. Uploads the audio file.
     *  2. Creates a transcription job.
     *  3. Polls status until Completed or Error.
     *  4. Retrieves and returns the transcript tokens.
     *  5. Deletes the transcription and uploaded file.
     *
     * @param file_path        Path to the local audio file.
     * @param config           Transcription configuration (api_key is ignored;
     *                         uses the key passed to the constructor).
     * @param poll_interval_ms Polling interval in milliseconds (default: 1000).
     * @return                 Completed transcript tokens.
     * @throws std::runtime_error on any failure.
     */
    std::vector<Token> transcribeFile(
        const std::string& file_path,
        AsyncConfig        config,
        int                poll_interval_ms = 1000);

private:
    std::unique_ptr<AsyncClientImpl> impl_;
};

} // namespace soniox
