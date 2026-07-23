#pragma once

#include "websocket_transport.hpp"

#include <memory>

namespace soniox::transport {

struct LwsWebSocketTransportImpl;

/// libwebsockets-backed WebSocket transport (Linux / macOS).
class LwsWebSocketTransport final : public IWebSocketTransport {
public:
    /* caFilePath: CA bundle (PEM) consulted for the TLS handshake. mbedTLS (our TLS
       backend everywhere except Windows) ships with no built-in trust anchors, so on
       platforms where the OS trust store isn't visible to it (e.g. Android) this must
       be set explicitly; leave empty to use the platform default. */
    explicit LwsWebSocketTransport(std::string caFilePath = {});
    ~LwsWebSocketTransport() override;

    void setOnOpen(OpenHandler handler) override;
    void setOnTextMessage(TextMessageHandler handler) override;
    void setOnBinaryMessage(BinaryMessageHandler handler) override;
    void setOnError(ErrorHandler handler) override;
    void setOnClose(CloseHandler handler) override;

    /// @throws std::runtime_error on DNS, TLS, or LWS protocol errors.
    void connect(const WebSocketConnectOptions& options) override;

    void sendText(const std::string& message) override;
    void sendBinary(const std::vector<std::uint8_t>& payload) override;
    void close() override;
    bool isOpen() const override;

private:
    std::unique_ptr<LwsWebSocketTransportImpl> _impl;
};

} // namespace soniox::transport
