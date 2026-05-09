# Soniox API Coverage

This file tracks implemented Soniox STT/TTS features in the current rewrite.

## STT REST

- `POST /v1/files` upload file
- `GET /v1/files/{id}` get file metadata
- `GET /v1/files/{id}/url` get temporary file URL
- `GET /v1/files/count` count files
- `DELETE /v1/files/{id}` delete file
- `GET /v1/files` list files
- `POST /v1/transcriptions` create transcription
- `GET /v1/transcriptions/{id}` get transcription
- `GET /v1/transcriptions/{id}/transcript` get transcript tokens
- `GET /v1/transcriptions/count` count transcriptions
- `DELETE /v1/transcriptions/{id}` delete transcription
- `GET /v1/transcriptions` list transcriptions
- `GET /v1/models` list models

Typed response support added in `SttRestClient` for:

- `uploadFileTyped`
- `getFileTyped`
- `getFileUrlTyped`
- `getFilesCountTyped`
- `listFilesTyped`
- `createTranscriptionTyped`
- `getTranscriptionTyped`
- `getTranscriptionTranscriptTyped`
- `getTranscriptionsCountTyped`
- `listTranscriptionsTyped`

Typed list methods now return strong typed collections:

- `listFilesTyped` -> `SttListFilesTypedResult` with `std::vector<SttFile>`
- `listTranscriptionsTyped` -> `SttListTranscriptionsTypedResult` with `std::vector<SttTranscription>`

## STT Realtime (WebSocket)

- connect and send session config
- send audio chunks (binary)
- send end-of-audio (empty binary frame)
- send manual finalize message
- send keepalive message
- receive raw server messages via callback
- error and close callbacks

## TTS REST

- `POST /v1/speech` generate speech
- response trailers propagated by transport
- `GET /v1/models` list models

Typed response support added in `TtsRestClient` for:

- `getModelsTyped`

## TTS Realtime (WebSocket)

- connect and send stream-start config
- send text increments
- send keepalive message
- cancel stream
- close stream
- receive raw server messages via callback
- receive parsed server messages via typed callback (`setOnParsedMessage`)
- error and close callbacks

## Auth

- `POST /v1/auth/temporary-api-key` create temporary API key

Typed response support added in `AuthClient` for:

- `createTemporaryApiKeyTyped`

## Compatibility surfaces

- `AsyncClient` implemented through `SttRestClient`
- `RealtimeClient` implemented through `SttRealtimeClient`

## Error handling

- REST clients now throw `SonioxApiException` for HTTP errors.
- Parsed metadata is available via `detail()`:
  - `status_code`
  - `error_type`
  - `message`
  - `request_id`
  - `validation_errors` (field-level validation failures when provided by API)
  - `raw_body`

## Examples in repo

- `examples/async`
- `examples/realtime`
- `examples/realtime_mic`
- `examples/tts_rest`
- `examples/tts_realtime`
