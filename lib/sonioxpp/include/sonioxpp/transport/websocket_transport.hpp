#pragma once

/// @file websocket_transport.hpp
/// @brief WebSocket transport interface and connection options.
///
/// `IWebSocketTransport` is the single extension point for WebSocket I/O in SonioxPP.
/// Platform-specific concrete implementations are selected automatically:
///  - **Linux / macOS** — `LwsWebSocketTransport` (libwebsockets)
///  - **Windows**       — `WinHttpWebSocketTransport` (WinHTTP)
///
/// Pass a custom `std::shared_ptr<IWebSocketTransport>` to `SttRealtimeClient` or
/// `TtsRealtimeClient` to override the default backend.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace soniox::transport {

/// Options passed to `IWebSocketTransport::connect`.
struct WebSocketConnectOptions {
    std::string url;                            ///< `wss://` URL to connect to
    std::map<std::string, std::string> headers; ///< Additional HTTP upgrade request headers (e.g. `User-Agent`)
};

/// Event-driven WebSocket transport interface.
///
/// ### Lifecycle
/// 1. Register event handlers with `setOn*` — before calling `connect`.
/// 2. Call `connect(options)` to initiate the TLS + WebSocket handshake.
///    `connect` **blocks** until the connection is established (or throws).
/// 3. After `connect` returns, the implementation spawns an internal receive
///    loop.  Incoming frames are dispatched to the registered handlers from
///    that loop's thread.
/// 4. Call `sendText` / `sendBinary` to push frames (thread-safe).
/// 5. Call `close` to initiate a graceful close handshake; `setOnClose`
///    will be called once the connection is fully torn down.
///
/// ### Thread safety
/// `sendText`, `sendBinary`, `close`, and `isOpen` are safe to call from
/// any thread.  Handler callbacks are always invoked from the internal
/// receive-loop thread — do not call `connect` from within a handler.
class IWebSocketTransport {
public:
    /// Called once when the WebSocket handshake completes successfully.
    using OpenHandler = std::function<void()>;

    /// Called for each incoming UTF-8 text frame.
    using TextMessageHandler = std::function<void(const std::string&)>;

    /// Called for each incoming binary frame.
    using BinaryMessageHandler = std::function<void(const std::vector<std::uint8_t>&)>;

    /// Called when the connection encounters an unrecoverable error.
    /// The string argument is a human-readable description.
    /// `CloseHandler` is always called after this.
    using ErrorHandler = std::function<void(const std::string&)>;

    /// Called once after the connection is fully closed (normal or error).
    using CloseHandler = std::function<void()>;

    virtual ~IWebSocketTransport() = default;

    /// Register a handler invoked when the WebSocket handshake completes.
    virtual void setOnOpen(OpenHandler handler) = 0;

    /// Register a handler invoked for each incoming text frame.
    virtual void setOnTextMessage(TextMessageHandler handler) = 0;

    /// Register a handler invoked for each incoming binary frame.
    virtual void setOnBinaryMessage(BinaryMessageHandler handler) = 0;

    /// Register a handler invoked on connection errors.
    virtual void setOnError(ErrorHandler handler) = 0;

    /// Register a handler invoked when the connection is fully closed.
    virtual void setOnClose(CloseHandler handler) = 0;

    /// Connect to `options.url`, completing the TLS and WebSocket upgrade.
    ///
    /// Blocks until the handshake succeeds (after which `OpenHandler` fires)
    /// or throws `std::runtime_error` on failure.
    ///
    /// @throws std::runtime_error on DNS, TLS, or protocol errors.
    virtual void connect(const WebSocketConnectOptions& options) = 0;

    /// Send a UTF-8 text frame.  Safe to call from any thread after `connect`.
    virtual void sendText(const std::string& message) = 0;

    /// Send a binary frame.  An empty `payload` signals end-of-audio to the
    /// Soniox STT endpoint.  Safe to call from any thread after `connect`.
    virtual void sendBinary(const std::vector<std::uint8_t>& payload) = 0;

    /// Initiate a graceful WebSocket close handshake.
    /// `CloseHandler` is invoked once the close is complete.
    virtual void close() = 0;

    /// Returns `true` if the connection is currently open and ready to send.
    virtual bool isOpen() const = 0;
};

} // namespace soniox::transport
