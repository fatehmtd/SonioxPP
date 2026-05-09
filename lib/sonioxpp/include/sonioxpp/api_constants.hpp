#pragma once

/// @file api_constants.hpp
/// @brief String constants for Soniox API model IDs, audio formats, and usage types.
///
/// All identifiers are `constexpr const char*` and are intended to be passed directly
/// into the corresponding config/request fields of the client classes.
///
/// @see https://soniox.com/docs/models

namespace soniox::stt {

/// Model identifiers for the Soniox Speech-to-Text API.
namespace models {

/// Real-time WebSocket model — current production version.
/// Used with `RealtimeConfig::model` and `SttRealtimeConfig::model`.
constexpr const char* realtime_v4 = "stt-rt-v4";

/// Real-time WebSocket model — previous version (alias for v4, deprecated).
constexpr const char* realtime_v3 = "stt-rt-v3";

/// Async REST model — current production version.
/// Used with `AsyncConfig::model` and `SttCreateTranscriptionRequest::model`.
constexpr const char* async_v4 = "stt-async-v4";

/// Async REST model — previous version (alias for v4, deprecated).
constexpr const char* async_v3 = "stt-async-v3";

} // namespace models

/// Audio format identifiers accepted by the Soniox STT API.
///
/// When `pcm_s16le` is chosen, `sample_rate` and `num_channels` **must** also be set.
/// All other formats are auto-detected or container-based and do not require those fields.
namespace audio_formats {

/// Automatic format detection (default).
/// Supports: aac, aiff, amr, asf, flac, mp3, ogg, wav, webm.
constexpr const char* auto_detect = "auto";

/// Raw PCM — signed 16-bit little-endian. Requires `sample_rate` and `num_channels`.
constexpr const char* pcm_s16le = "pcm_s16le";

/// Raw PCM — signed 16-bit big-endian. Requires `sample_rate` and `num_channels`.
constexpr const char* pcm_s16be = "pcm_s16be";

/// WAV container (auto-detects encoding from the header).
constexpr const char* wav = "wav";

/// MP3 compressed audio.
constexpr const char* mp3 = "mp3";

/// OGG container (typically Vorbis or Opus).
constexpr const char* ogg = "ogg";

/// FLAC lossless audio.
constexpr const char* flac = "flac";

} // namespace translation_types

/// Transcription mode strings (informational; not sent in the client config).
namespace transcription_modes {

constexpr const char* realtime = "real_time";
constexpr const char* async = "async";

} // namespace transcription_modes

/// Translation type identifiers for `Translation::type`.
namespace translation_types {

/// Translate speech in one direction: source → `language_a`.
constexpr const char* one_way = "one_way";

/// Translate speech bi-directionally between `language_a` and `language_b`.
constexpr const char* two_way = "two_way";

} // namespace audio_formats

} // namespace soniox::stt

namespace soniox::tts {

/// Model identifiers for the Soniox Text-to-Speech API.
namespace models {

/// Real-time TTS WebSocket model — current production version.
/// Used with `TtsGenerateRequest::model` and `TtsRealtimeStreamConfig::model`.
constexpr const char* realtime_v1 = "tts-rt-v1";

} // namespace models

/// Audio format identifiers for TTS output.
///
/// Default sample rates apply when `sample_rate` is left at 0:
///  - Most formats default to 24 000 Hz.
///  - `pcm_mulaw` and `pcm_alaw` default to 8 000 Hz.
namespace audio_formats {

/// WAV container, 24 kHz default.
constexpr const char* wav = "wav";

/// MP3 compressed audio, 24 kHz default. Supports `bitrate` override.
constexpr const char* mp3 = "mp3";

/// Opus compressed audio, 24 kHz default. Supports `bitrate` override.
constexpr const char* opus = "opus";

/// FLAC lossless audio, 24 kHz default.
constexpr const char* flac = "flac";

/// Raw PCM — signed 16-bit little-endian, 24 kHz default.
constexpr const char* pcm_s16le = "pcm_s16le";

/// Raw PCM — signed 16-bit big-endian, 24 kHz default.
constexpr const char* pcm_s16be = "pcm_s16be";

} // namespace audio_formats

} // namespace soniox::tts

namespace soniox::auth {

/// `usage_type` values for `TemporaryApiKeyRequest`.
/// Controls which Soniox endpoint the temporary key is allowed to call.
namespace temporary_api_key_usage {

/// Allows the key to open an STT real-time WebSocket session.
constexpr const char* transcribe_websocket = "transcribe_websocket";

/// Allows the key to open a TTS real-time WebSocket session.
constexpr const char* tts_rt = "tts_rt";

} // namespace temporary_api_key_usage

} // namespace soniox::auth
