# Transport Layer

The library abstracts all network I/O behind two thin interfaces so Soniox protocol logic is fully decoupled from HTTP/WebSocket libraries.

---

## Interfaces

### `soniox::transport::IHttpTransport`

Defined in [transport/http_transport.hpp](../lib/sonioxpp/include/sonioxpp/transport/http_transport.hpp).

```cpp
class IHttpTransport {
public:
    virtual HttpResponse send(const HttpRequest& request) = 0;
};
```

Key types:

| Type | Description |
| --- | --- |
| `HttpRequest` | Method, URL, headers, JSON body, multipart files, timeout |
| `HttpResponse` | Status code, headers, trailers, body bytes |
| `MultipartFile` | A single part of a `multipart/form-data` upload |

HTTP-level errors (status ≥ 400) are **not** thrown by the transport; the REST clients inspect `status_code` and raise `SonioxApiException`.

Transport-level failures (DNS, TLS, I/O) throw `std::runtime_error`.

---

### `soniox::transport::IWebSocketTransport`

Defined in [transport/websocket_transport.hpp](../lib/sonioxpp/include/sonioxpp/transport/websocket_transport.hpp).

```cpp
class IWebSocketTransport {
public:
    virtual void setOnOpen(OpenHandler)              = 0;
    virtual void setOnTextMessage(TextMessageHandler) = 0;
    virtual void setOnBinaryMessage(BinaryMessageHandler) = 0;
    virtual void setOnError(ErrorHandler)            = 0;
    virtual void setOnClose(CloseHandler)            = 0;

    virtual void connect(const WebSocketConnectOptions& options) = 0;
    virtual void sendText(const std::string& message)            = 0;
    virtual void sendBinary(const std::vector<uint8_t>& payload) = 0;
    virtual void close()                                         = 0;
    virtual bool isOpen() const                                  = 0;
};
```

Lifecycle:

1. Register handlers with `setOn*` **before** calling `connect`.
2. `connect` blocks until the TLS + WebSocket handshake completes (fires `OpenHandler`) or throws `std::runtime_error`.
3. An internal receive loop dispatches incoming frames to the registered handlers.
4. `sendText` / `sendBinary` / `close` are thread-safe and may be called from any thread after `connect` returns.
5. `CloseHandler` fires once after the connection is fully torn down.

---

## Concrete Implementations

### `CurlHttpTransport` (all platforms)

- **Backend:** libcurl (fetched automatically via CMake FetchContent)
- **TLS:** OpenSSL on Linux/macOS; Schannel-linked libcurl on Windows
- **Supports:** JSON requests, `multipart/form-data` file uploads, response headers, TTS HTTP trailers (`X-Tts-Error-Code` / `X-Tts-Error-Message`)
- **Thread model:** stateless — each `send()` call creates and releases a CURL easy handle

---

### `LwsWebSocketTransport` (Linux / macOS)

- **Backend:** libwebsockets 4.3 (fetched automatically via CMake FetchContent)
- **TLS:** via the system OpenSSL linked into libwebsockets
- **Thread model:** `connect` blocks until handshake completes; an internal service thread dispatches callbacks; `sendText`/`sendBinary`/`close` are thread-safe via a write queue

---

### `WinHttpWebSocketTransport` (Windows only)

- **Backend:** Windows WinHTTP API (`winhttp.h`)
- **TLS:** Windows Schannel — no OpenSSL dependency
- **Thread model:** `connect` blocks until HTTP upgrade completes; an internal receive thread dispatches callbacks; `sendText`/`sendBinary`/`close` are thread-safe via a mutex

---

## Using a Custom Transport

Pass a `std::shared_ptr<IHttpTransport>` or `std::shared_ptr<IWebSocketTransport>` to any client constructor:

```cpp
// Custom HTTP transport (e.g. wrapping libcpr, Boost.Beast, etc.)
auto my_http = std::make_shared<MyHttpTransport>();
soniox::SttRestClient   stt("key", my_http);
soniox::TtsRestClient   tts("key", my_http);
soniox::AuthClient      auth("key", my_http);

// Custom WebSocket transport
auto my_ws = std::make_shared<MyWebSocketTransport>();
soniox::SttRealtimeClient stt_rt(my_ws);
soniox::TtsRealtimeClient tts_rt(my_ws);
```

---

## Client → Transport Mapping

| Client | HTTP transport | WebSocket transport |
| --- | --- | --- |
| `SttRestClient` | `CurlHttpTransport` | — |
| `AsyncClient` | (delegates to `SttRestClient`) | — |
| `TtsRestClient` | `CurlHttpTransport` | — |
| `AuthClient` | `CurlHttpTransport` | — |
| `SttRealtimeClient` | — | `LwsWebSocketTransport` (Linux/macOS) · `WinHttpWebSocketTransport` (Windows) |
| `RealtimeClient` | — | (delegates to `SttRealtimeClient`) |
| `TtsRealtimeClient` | — | `LwsWebSocketTransport` (Linux/macOS) · `WinHttpWebSocketTransport` (Windows) |
