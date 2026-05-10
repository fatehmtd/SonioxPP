#pragma once

#include "types.hpp"

#include <memory>
#include <vector>
#include <cstdint>

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

class RealtimeClientImpl;

/// Real-time WebSocket STT client. Not copyable; movable.
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

    void setOnTokens(OnTokensCallback onTokensCallback);
    void setOnFinished(OnFinishedCallback onFinishedCallback);
    void setOnError(OnErrorCallback onErrorCallback);

    // -----------------------------------------------------------------------
    // Connection & streaming
    // -----------------------------------------------------------------------

    /// @throws std::runtime_error on connection failure.
    void connect(const RealtimeConfig& config);

    void sendAudio(const uint8_t* data, size_t size);
    void sendAudio(const std::vector<uint8_t>& data);

    /// Send an empty binary frame to signal end-of-audio.
    void sendEndOfAudio();

    /// Block until the session finishes or an error occurs.
    void run();

    void close();

private:
    std::unique_ptr<RealtimeClientImpl> _impl;
};

} // namespace soniox
