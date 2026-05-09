#pragma once

/// @file winhttp_websocket_transport.hpp
/// @brief WinHTTP-based implementation of `IWebSocketTransport` (Windows only).
///
/// `WinHttpWebSocketTransport` is the default WebSocket backend used by
/// `SttRealtimeClient` and `TtsRealtimeClient` on Windows.
///
/// Features:
///  - TLS via Windows Schannel — no OpenSSL required
///  - Text and binary frame send/receive using the `WinHttpWebSocket*` API
///  - Internal receive thread — `connect` blocks until the HTTP upgrade
///    completes, then returns; callbacks fire from the receive thread
///  - Thread-safe `sendText`, `sendBinary`, and `close` protected by a mutex

#include "websocket_transport.hpp"

#include <atomic>
#include <memory>
#include <mutex>

namespace soniox::transport {

/// WebSocket transport backed by the Windows WinHTTP API.
///
/// Used automatically on Windows when no custom transport is supplied.
/// Not typically instantiated directly by application code.
class WinHttpWebSocketTransport final : public IWebSocketTransport {
public:
    WinHttpWebSocketTransport();
    ~WinHttpWebSocketTransport() override;

    /// @name IWebSocketTransport overrides
    /// @{
    void setOnOpen(OpenHandler handler) override;
    void setOnTextMessage(TextMessageHandler handler) override;
    void setOnBinaryMessage(BinaryMessageHandler handler) override;
    void setOnError(ErrorHandler handler) override;
    void setOnClose(CloseHandler handler) override;

    /// Connect to `options.url`, completing TLS + WebSocket handshake.
    /// Blocks until the connection is open (fires `OpenHandler`) or throws.
    /// @throws std::runtime_error on WinHTTP or TLS errors.
    void connect(const WebSocketConnectOptions& options) override;

    void sendText(const std::string& message) override;
    void sendBinary(const std::vector<std::uint8_t>& payload) override;
    void close() override;
    bool isOpen() const override;
    /// @}

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;

    mutable std::mutex _callbackMutex;
    OpenHandler _onOpen;
    TextMessageHandler _onText;
    BinaryMessageHandler _onBinary;
    ErrorHandler _onError;
    CloseHandler _onClose;

    std::atomic<bool> _isOpen{false};
};

} // namespace soniox::transport
