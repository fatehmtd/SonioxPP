# Transport Layer

This project now includes explicit transport interfaces so Soniox protocol logic can be implemented independently of HTTP/WebSocket libraries.

## Interfaces

- `soniox::transport::IHttpTransport`
  - request/response abstraction with status, headers, body, and trailer fields
  - multipart file support for STT file uploads
- `soniox::transport::IWebSocketTransport`
  - open/text/binary/error/close callbacks
  - explicit connect/send/close lifecycle

## Concrete implementations

- `soniox::transport::CurlHttpTransport`
  - backend: libcurl
  - supports JSON requests and multipart file uploads
  - captures response headers and known TTS trailer keys
- `soniox::transport::WebSocketppTransport`
  - backend: websocketpp over TLS
  - supports text and binary frames with callback-based events

## Current status

- `soniox::SttRestClient` (REST STT operations)
- `soniox::SttRealtimeClient` (WebSocket STT streaming)
- `soniox::TtsRestClient` (REST TTS generation)
- `soniox::TtsRealtimeClient` (WebSocket TTS streaming)
- `soniox::AuthClient` (temporary API key)

Legacy compatibility surfaces now delegate to the new clients:

- `soniox::AsyncClient` -> `soniox::SttRestClient`
- `soniox::RealtimeClient` -> `soniox::SttRealtimeClient`

This keeps existing include paths functional while transport-backed implementations become the primary path.
