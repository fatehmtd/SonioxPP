#pragma once

/// @file lws_websocket_transport.hpp
/// @brief libwebsockets-based implementation of `IWebSocketTransport` (Linux / macOS).
///
/// `LwsWebSocketTransport` is the default WebSocket backend used by
/// `SttRealtimeClient` and `TtsRealtimeClient` on Linux and macOS.
///
/// Features:
///  - TLS via the system OpenSSL linked into libwebsockets
///  - Text and binary frame send/receive
///  - Internal service thread — `connect` blocks until the handshake
///    completes, then returns; callbacks fire from the service thread
///  - Thread-safe `sendText`, `sendBinary`, and `close` via an internal
///    write queue and lws `lws_callback_on_writable` scheduling

#include "websocket_transport.hpp"

#include <memory>

namespace soniox::transport {

/// Opaque implementation detail — not part of the public API.
struct LwsWebSocketTransportImpl;

/// WebSocket transport backed by libwebsockets (LWS).
///
/// Used automatically on Linux/macOS when no custom transport is supplied.
/// Not typically instantiated directly by application code.
class LwsWebSocketTransport final : public IWebSocketTransport {
public:
    LwsWebSocketTransport();
    ~LwsWebSocketTransport() override;

    /// @name IWebSocketTransport overrides
    /// @{
    void setOnOpen(OpenHandler handler) override;
    void setOnTextMessage(TextMessageHandler handler) override;
    void setOnBinaryMessage(BinaryMessageHandler handler) override;
    void setOnError(ErrorHandler handler) override;
    void setOnClose(CloseHandler handler) override;

    /// Connect to `options.url`, completing TLS + WebSocket handshake.
    /// Blocks until the connection is open (fires `OpenHandler`) or throws.
    /// @throws std::runtime_error on DNS, TLS, or LWS protocol errors.
    void connect(const WebSocketConnectOptions& options) override;

    void sendText(const std::string& message) override;
    void sendBinary(const std::vector<std::uint8_t>& payload) override;
    void close() override;
    bool isOpen() const override;
    /// @}

private:
    std::unique_ptr<LwsWebSocketTransportImpl> _impl;
};

} // namespace soniox::transport
