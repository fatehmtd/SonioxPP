#pragma once

#include "types.hpp"

#include <memory>
#include <vector>
#include <cstdint>

/// @file realtime_client.hpp
/// @brief Real-time WebSocket STT client for the Soniox API.

namespace soniox {


// ---------------------------------------------------------------------------
// API endpoint constants
// ---------------------------------------------------------------------------

namespace endpoints {
    constexpr const char* REALTIME_HOST = "stt-rt.soniox.com";
    constexpr const char* REALTIME_PORT = "443";
    constexpr const char* REALTIME_PATH = "/transcribe-websocket";
} // namespace endpoints

// ---------------------------------------------------------------------------

// Forward declaration of the implementation class (Pimpl idiom)
class RealtimeClientImpl;

/**
 * @brief Streams audio to Soniox's real-time WebSocket endpoint and delivers
 *        transcription tokens via callbacks as they arrive.
 *
 * ### Endpoint
 * `wss://stt-rt.soniox.com/transcribe-websocket`
 *
 * ### Typical usage
 * @code
 *   soniox::RealtimeConfig cfg;
 *   cfg.api_key       = "YOUR_KEY";
 *   cfg.language_hints = {"en"};
 *
 *   soniox::RealtimeClient client;
 *   client.setOnTokens([](auto& tokens, bool final) { ... });
 *   client.setOnFinished([]{ ... });
 *   client.setOnError([](const soniox::RealtimeError& err){ ... });
 *
 *   client.connect(cfg);
 *
 *   // Optionally send audio from a separate thread:
 *   std::thread sender([&]{
 *       client.sendAudio(pcm_data.data(), pcm_data.size());
 *       client.sendEndOfAudio();
 *   });
 *
 *   client.run();   // blocks until session ends
 *   sender.join();
 * @endcode
 *
 * @note RealtimeClient is not copyable; it is movable.
 */
class RealtimeClient {
public:
    RealtimeClient();
    ~RealtimeClient();

    RealtimeClient(const RealtimeClient&)            = delete;
    RealtimeClient& operator=(const RealtimeClient&) = delete;
    RealtimeClient(RealtimeClient&&)            noexcept;
    RealtimeClient& operator=(RealtimeClient&&) noexcept;

    // -----------------------------------------------------------------------
    // Callbacks (register before calling connect())
    // -----------------------------------------------------------------------

    /// Called for every token batch received from the server.
    /// @param cb  Callback receiving token list and a flag indicating whether
    ///            any token in this batch is final.
    void setOnTokens(OnTokensCallback onTokensCallback);

    /// Called once when the server signals `finished: true`.
    void setOnFinished(OnFinishedCallback onFinishedCallback);

    /// Called on any network or protocol error.
    void setOnError(OnErrorCallback onErrorCallback);

    // -----------------------------------------------------------------------
    // Connection & streaming
    // -----------------------------------------------------------------------

    /// Resolve, connect, perform TLS + WebSocket handshake, and send the
    /// initial JSON configuration to the server.
    /// @throws std::runtime_error on any connection failure.
    void connect(const RealtimeConfig& config);

    /// Send a chunk of raw audio bytes (binary WebSocket frame).
    /// May be called from any thread after connect().
    void sendAudio(const uint8_t* data, size_t size);

    /// Convenience overload accepting a vector.
    void sendAudio(const std::vector<uint8_t>& data);

    /// Send an empty binary frame to signal end-of-audio.
    /// The server will finalize the session and send `finished: true`.
    void sendEndOfAudio();

    /// Block until the session finishes or an error occurs.
    /// Invoke callbacks on incoming messages internally.
    /// Call from the main thread or a dedicated receiver thread.
    void run();

    /// Close the WebSocket connection immediately (without a graceful drain).
    void close();

private:
    std::unique_ptr<RealtimeClientImpl> impl_;
};

} // namespace soniox
