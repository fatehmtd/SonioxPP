# Soniox API Coverage

This file tracks implemented Soniox STT/TTS features in the current rewrite.

Last synced with the Soniox API docs on 2026-07-18.

## Models

- `stt-rt-v5` / `stt-async-v5` are the latest models and the library defaults.
- `stt-rt-v4` / `stt-async-v4` remain as aliases for v5.
- v3 models were removed from the Soniox service on 2026-02-28 and their
  constants have been dropped.

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

Session config supports the full current field set: `language_hints`,
`language_hints_strict`, `enable_speaker_diarization`,
`enable_language_identification`, `enable_endpoint_detection`,
`max_endpoint_delay_ms`, `endpoint_sensitivity` (v5),
`endpoint_latency_adjustment_level` (v5), `context`, `translation`,
and `client_reference_id`.

Parsed realtime tokens expose `text`, `is_final`, `speaker`, `language`,
`source_language`, `translation_status`, `start_ms`, `end_ms`, and
`confidence`.

## TTS REST

- `POST https://tts-rt.soniox.com/tts` generate speech
  (supports `speed`, `bitrate`, `client_reference_id`)
- response trailers propagated by transport
- `GET /v1/tts-models` list models

Voice cloning (`https://api.soniox.com/v1/voices`):

- `POST /v1/voices` create voice (multipart name + reference audio)
- `GET /v1/voices` list voices (paginated)
- `GET /v1/voices/count` count voices
- `GET /v1/voices/{id}` get voice
- `POST /v1/voices/{id}/recompute` recompute voice for newer models
- `DELETE /v1/voices/{id}` delete voice

Typed response support added in `TtsRestClient` for:

- `getModelsTyped`
- `createVoiceTyped`
- `listVoicesTyped`
- `getVoicesCountTyped`
- `getVoiceTyped`
- `recomputeVoiceTyped`

## TTS Realtime (WebSocket)

- connect and send stream-start config
  (supports `speed`, `return_timestamps`, `client_reference_id`)
- send text increments
- send keepalive message
- cancel stream
- close stream
- receive raw server messages via callback
- receive parsed server messages via typed callback (`setOnParsedMessage`),
  including `audio_end` and character-level timestamps
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

## Not yet implemented

- `GET /v1/usage-logs` usage logs
- `GET /v1/concurrency-limits` concurrency limits

## Examples in repo

- `examples/async`
- `examples/async_timestamps`
- `examples/realtime`
- `examples/realtime_mic`
- `examples/realtime_translation`
- `examples/tts_rest`
- `examples/tts_realtime`
- `examples/tts_multiplex`
- `examples/tts_streaming_text`
