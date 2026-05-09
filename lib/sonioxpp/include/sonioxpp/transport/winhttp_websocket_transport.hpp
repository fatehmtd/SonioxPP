#pragma once

#include "websocket_transport.hpp"

#include <atomic>
#include <memory>
#include <mutex>

namespace soniox::transport {

class WinHttpWebSocketTransport final : public IWebSocketTransport {
public:
    WinHttpWebSocketTransport();
    ~WinHttpWebSocketTransport() override;

    void setOnOpen(OpenHandler handler) override;
    void setOnTextMessage(TextMessageHandler handler) override;
    void setOnBinaryMessage(BinaryMessageHandler handler) override;
    void setOnError(ErrorHandler handler) override;
    void setOnClose(CloseHandler handler) override;

    void connect(const WebSocketConnectOptions& options) override;
    void sendText(const std::string& message) override;
    void sendBinary(const std::vector<std::uint8_t>& payload) override;
    void close() override;
    bool isOpen() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    mutable std::mutex callback_mutex_;
    OpenHandler on_open_;
    TextMessageHandler on_text_;
    BinaryMessageHandler on_binary_;
    ErrorHandler on_error_;
    CloseHandler on_close_;

    std::atomic<bool> is_open_{false};
};

} // namespace soniox::transport
