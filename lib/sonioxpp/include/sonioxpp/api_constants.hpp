#pragma once

namespace soniox::stt {

namespace models {

constexpr const char* realtime_v4 = "stt-rt-v4";
constexpr const char* realtime_v3 = "stt-rt-v3"; // deprecated alias for v4
constexpr const char* async_v4 = "stt-async-v4";
constexpr const char* async_v3 = "stt-async-v3"; // deprecated alias for v4

} // namespace models

/* All pcm_* formats require `sample_rate` and `num_channels` to be set.
   Container formats (wav, mp3, ogg, flac) are auto-detected; use auto_detect
   to let the server pick the format from the stream header. */
namespace audio_formats {

constexpr const char* auto_detect = "auto";

constexpr const char* pcm_s16le  = "pcm_s16le";
constexpr const char* pcm_s16be  = "pcm_s16be";
constexpr const char* pcm_s32le  = "pcm_s32le";
constexpr const char* pcm_s32be  = "pcm_s32be";
constexpr const char* pcm_f32le  = "pcm_f32le";
constexpr const char* pcm_f32be  = "pcm_f32be";
constexpr const char* mulaw      = "mulaw";
constexpr const char* alaw       = "alaw";

constexpr const char* wav  = "wav";
constexpr const char* mp3  = "mp3";
constexpr const char* ogg  = "ogg";
constexpr const char* flac = "flac";

} // namespace audio_formats

namespace transcription_modes {

constexpr const char* realtime = "real_time";
constexpr const char* async = "async";

} // namespace transcription_modes

namespace translation_types {

constexpr const char* one_way = "one_way";
constexpr const char* two_way = "two_way";

} // namespace translation_types

} // namespace soniox::stt

namespace soniox::tts {

namespace models {

constexpr const char* realtime_v1 = "tts-rt-v1";

} // namespace models

/* Default sample rate is 24 000 Hz for all formats except pcm_mulaw and pcm_alaw,
   which are fixed at 8 000 Hz. */
namespace audio_formats {

constexpr const char* wav      = "wav";
constexpr const char* mp3      = "mp3";
constexpr const char* opus     = "opus";
constexpr const char* flac     = "flac";
constexpr const char* aac      = "aac";
constexpr const char* pcm_s16le  = "pcm_s16le";
constexpr const char* pcm_f32le  = "pcm_f32le";
constexpr const char* pcm_mulaw  = "pcm_mulaw"; // fixed 8 000 Hz
constexpr const char* pcm_alaw   = "pcm_alaw";  // fixed 8 000 Hz

} // namespace audio_formats

} // namespace soniox::tts

namespace soniox::auth {

namespace temporary_api_key_usage {

constexpr const char* transcribe_websocket = "transcribe_websocket";
constexpr const char* tts_rt = "tts_rt";

} // namespace temporary_api_key_usage

} // namespace soniox::auth
