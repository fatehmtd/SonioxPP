#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace soniox::transport {

struct WebSocketConnectOptions {
    std::string url;
    std::map<std::string, std::string> headers;
};

class IWebSocketTransport {
public:
    using OpenHandler = std::function<void()>;
    using TextMessageHandler = std::function<void(const std::string&)>;
    using BinaryMessageHandler = std::function<void(const std::vector<std::uint8_t>&)>;
    using ErrorHandler = std::function<void(const std::string&)>;
    using CloseHandler = std::function<void()>;

    virtual ~IWebSocketTransport() = default;

    virtual void setOnOpen(OpenHandler handler) = 0;
    virtual void setOnTextMessage(TextMessageHandler handler) = 0;
    virtual void setOnBinaryMessage(BinaryMessageHandler handler) = 0;
    virtual void setOnError(ErrorHandler handler) = 0;
    virtual void setOnClose(CloseHandler handler) = 0;

    virtual void connect(const WebSocketConnectOptions& options) = 0;
    virtual void sendText(const std::string& message) = 0;
    virtual void sendBinary(const std::vector<std::uint8_t>& payload) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
};

} // namespace soniox::transport
