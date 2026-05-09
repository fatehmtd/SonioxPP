/**
 * @file main.cpp
 * @brief Soniox real-time streaming with live microphone capture via miniaudio.
 *
 * Captures PCM audio from the system's default input device using miniaudio
 * and streams it directly to Soniox's WebSocket endpoint.  Transcription
 * tokens are printed as they arrive; a live preview line is updated in-place
 * and final tokens are committed to the running transcript.
 *
 * Usage:
 *   export SONIOX_API_KEY=<your_key>
 *   ./soniox_realtime_mic [options]
 *
 * Press Enter or Ctrl+C to stop recording and wait for the final transcript.
 *
 * Options:
 *   --lang <code>   Comma-separated language hints (default: en)
 *   --diarize       Enable speaker diarization
 *   --lang-id       Enable per-token language identification
 *   --debug         Enable debug logging
 *
 * Audio captured at 16 kHz, mono, 16-bit signed PCM — the most efficient
 * format for speech recognition and a perfect match for pcm_s16le.
 */

// Compile miniaudio implementation in this translation unit
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <CLI/CLI.hpp>
#include <sonioxpp/soniox.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Signal handling (Ctrl+C sets this flag)
// ---------------------------------------------------------------------------

static std::atomic<bool> g_stop{false};

static void onSignal(int) noexcept
{
    g_stop.store(true, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Capture state — shared between miniaudio's audio thread and the sender
// ---------------------------------------------------------------------------

struct CaptureState {
    std::mutex                       mtx;
    std::condition_variable          cv;
    std::deque<std::vector<uint8_t>> queue;
    bool                             stopped{false};
};

/**
 * @brief miniaudio data callback — invoked on miniaudio's internal audio thread.
 *
 * Copies each captured PCM buffer into the shared queue so the sender thread
 * can forward it to Soniox without blocking the real-time audio thread.
 */
static void captureCallback(ma_device* device, void* /*output*/,
                             const void* input, ma_uint32 frame_count)
{
    auto* state = static_cast<CaptureState*>(device->pUserData);
    if (!input || !state) return;

    const size_t   bytes = static_cast<size_t>(frame_count) * sizeof(int16_t);
    const uint8_t* src   = static_cast<const uint8_t*>(input);

    {
        std::lock_guard<std::mutex> lk(state->mtx);
        if (state->stopped) return;
        state->queue.emplace_back(src, src + bytes);
    }
    state->cv.notify_one();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    std::signal(SIGINT, onSignal);

    const char* api_key_env = std::getenv("SONIOX_API_KEY");
    if (!api_key_env) {
        std::cerr << "Error: SONIOX_API_KEY environment variable is not set.\n";
        return 1;
    }

    std::vector<std::string> langs{"en"};
    bool                     diarize = false;
    bool                     lang_id = false;
    bool                     debug   = false;

    CLI::App app{"Transcribe live microphone audio with Soniox real-time STT"};
    app.add_option("--lang",   langs,   "Language hints (comma-separated, e.g. en,es)")->delimiter(',');
    app.add_flag("--diarize",  diarize, "Enable speaker diarization");
    app.add_flag("--lang-id",  lang_id, "Enable per-token language identification");
    app.add_flag("--debug",    debug,   "Enable debug logging");
    CLI11_PARSE(app, argc, argv);

    if (debug) spdlog::set_level(spdlog::level::debug);

    soniox::RealtimeConfig config;
    config.api_key                        = api_key_env;
    config.model                          = soniox::stt::models::realtime_v4;
    config.audio_format                   = soniox::stt::audio_formats::pcm_s16le;
    config.sample_rate                    = 16000;
    config.num_channels                   = 1;
    config.language_hints                 = langs;
    config.enable_speaker_diarization     = diarize;
    config.enable_language_identification = lang_id;
    config.enable_endpoint_detection      = true;

    std::string        transcript;
    std::mutex         print_mtx;
    std::atomic<bool>  session_error{false};

    soniox::RealtimeClient client;

    client.setOnTokens([&](const std::vector<soniox::Token>& tokens, bool has_final) {
        std::lock_guard<std::mutex> lk(print_mtx);

        if (has_final)
            for (const auto& tok : tokens)
                if (tok.is_final) transcript += tok.text;

        std::string preview = transcript;
        if (!has_final)
            for (const auto& tok : tokens) preview += tok.text;

        std::cout << "\r\033[K"
                  << (has_final ? "[F] " : "[~] ") << preview
                  << std::flush;
    });

    client.setOnFinished([&] {
        std::lock_guard<std::mutex> lk(print_mtx);
        std::cout << "\n\n=== Session complete ===\n" << transcript << "\n";
    });

    client.setOnError([&](const soniox::RealtimeError& err) {
        {
            std::lock_guard<std::mutex> lk(print_mtx);
            std::cerr << "\n[Error " << err.error_code << "] " << err.error_message << "\n";
        }
        session_error.store(true, std::memory_order_relaxed);
        g_stop.store(true, std::memory_order_relaxed);
    });

    try {
        std::cout << "Connecting to Soniox real-time API...\n";
        client.connect(config);
        std::cout << "Connected.\n";
    } catch (const std::exception& e) {
        std::cerr << "Connection failed: " << e.what() << "\n";
        return 1;
    }

    static constexpr ma_uint32 SAMPLE_RATE = 16000;
    static constexpr ma_uint32 CHANNELS    = 1;

    CaptureState capture_state;

    ma_device_config device_config   = ma_device_config_init(ma_device_type_capture);
    device_config.capture.format     = ma_format_s16;
    device_config.capture.channels   = CHANNELS;
    device_config.sampleRate         = SAMPLE_RATE;
    device_config.dataCallback       = captureCallback;
    device_config.pUserData          = &capture_state;

    ma_device device;
    if (ma_device_init(nullptr, &device_config, &device) != MA_SUCCESS) {
        std::cerr << "Failed to initialize audio capture device.\n"
                  << "Ensure a microphone is connected and accessible.\n";
        return 1;
    }

    std::thread sender([&] {
        while (true) {
            std::vector<uint8_t> chunk;
            {
                std::unique_lock<std::mutex> lk(capture_state.mtx);
                capture_state.cv.wait(lk, [&] {
                    return !capture_state.queue.empty() || capture_state.stopped;
                });
                if (capture_state.queue.empty()) break;
                chunk = std::move(capture_state.queue.front());
                capture_state.queue.pop_front();
            }
            try { client.sendAudio(chunk); }
            catch (...) { break; }
        }
        if (!session_error.load(std::memory_order_relaxed)) {
            try { client.sendEndOfAudio(); } catch (...) {}
        }
    });

    std::thread receiver([&] {
        try {
            client.run();
        } catch (const std::exception& e) {
            std::cerr << "\nReceive error: " << e.what() << "\n";
        }
    });

    if (ma_device_start(&device) != MA_SUCCESS) {
        std::cerr << "Failed to start audio capture device.\n";
        ma_device_uninit(&device);
        {
            std::lock_guard<std::mutex> lk(capture_state.mtx);
            capture_state.stopped = true;
        }
        capture_state.cv.notify_all();
        sender.join();
        return 1;
    }

    std::cout << "Recording from microphone at " << SAMPLE_RATE
              << " Hz, mono, 16-bit PCM.\n"
              << "Press Enter or Ctrl+C to stop...\n\n";

    std::thread([] { std::cin.get(); g_stop.store(true, std::memory_order_relaxed); })
        .detach();

    while (!g_stop.load(std::memory_order_relaxed))
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::cout << "\nStopping microphone...\n";
    ma_device_stop(&device);

    {
        std::lock_guard<std::mutex> lk(capture_state.mtx);
        capture_state.stopped = true;
    }
    capture_state.cv.notify_all();
    sender.join();

    std::cout << "Waiting for final transcript...\n";
    receiver.join();

    ma_device_uninit(&device);
    return 0;
}
