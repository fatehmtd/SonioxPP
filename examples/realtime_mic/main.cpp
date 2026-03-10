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

    // Each frame is one int16_t sample (mono, s16)
    const size_t    bytes = static_cast<size_t>(frame_count) * sizeof(int16_t);
    const uint8_t*  src   = static_cast<const uint8_t*>(input);

    {
        std::lock_guard<std::mutex> lk(state->mtx);
        if (state->stopped) return;
        state->queue.emplace_back(src, src + bytes);
    }
    state->cv.notify_one();
}

// ---------------------------------------------------------------------------
// CLI helpers
// ---------------------------------------------------------------------------

static void printUsage(const char* prog)
{
    std::cerr
        << "Usage:\n"
        << "  " << prog << " [options]\n\n"
        << "Options:\n"
        << "  --lang <code>   Comma-separated language hints (default: en)\n"
        << "  --diarize       Enable speaker diarization\n"
        << "  --lang-id       Enable per-token language identification\n"
        << "  --debug         Enable debug logging\n\n"
        << "Press Enter or Ctrl+C to stop recording.\n";
}

static std::vector<std::string> splitLangs(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else           cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    // Register signal handler for Ctrl+C
    std::signal(SIGINT, onSignal);

    // --- API key ---
    const char* api_key_env = std::getenv("SONIOX_API_KEY");
    if (!api_key_env) {
        std::cerr << "Error: SONIOX_API_KEY environment variable is not set.\n";
        return 1;
    }

    // --- Parse arguments ---
    std::string lang_str = "en";
    bool        diarize  = false;
    bool        lang_id  = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if      (arg == "--lang"   && i + 1 < argc) lang_str = argv[++i];
        else if (arg == "--diarize")                diarize  = true;
        else if (arg == "--lang-id")               lang_id  = true;
        else if (arg == "--debug")
            spdlog::set_level(spdlog::level::debug);
        else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    // --- Build Soniox config ---
    soniox::RealtimeConfig config;
    config.api_key                        = api_key_env;
    config.model                          = "stt-rt-v4";
    config.audio_format                   = "pcm_s16le"; // raw 16-bit PCM from mic
    config.sample_rate                    = 16000;
    config.num_channels                   = 1;
    config.language_hints                 = splitLangs(lang_str);
    config.enable_speaker_diarization     = diarize;
    config.enable_language_identification = lang_id;
    config.enable_endpoint_detection      = true;

    // --- Transcript state ---
    std::string        transcript;
    std::mutex         print_mtx;
    std::atomic<bool>  session_error{false};

    // --- Set up Soniox client callbacks ---
    soniox::RealtimeClient client;

    client.setOnTokens([&](const std::vector<soniox::Token>& tokens, bool has_final) {
        std::lock_guard<std::mutex> lk(print_mtx);

        if (has_final)
            for (const auto& tok : tokens)
                if (tok.is_final) transcript += tok.text;

        // Build live preview: committed transcript + current non-final tail
        std::string preview = transcript;
        if (!has_final)
            for (const auto& tok : tokens) preview += tok.text;

        std::cout << "\r\033[K"                                   // erase line
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
        g_stop.store(true, std::memory_order_relaxed); // unblock main loop
    });

    // --- Connect to Soniox ---
    try {
        std::cout << "Connecting to Soniox real-time API...\n";
        client.connect(config);
        std::cout << "Connected.\n";
    } catch (const std::exception& e) {
        std::cerr << "Connection failed: " << e.what() << "\n";
        return 1;
    }

    // --- Configure miniaudio capture device ---
    // 16 kHz, mono, signed 16-bit PCM — ideal for speech-to-text
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

    // --- Sender thread: drain capture queue and forward audio to Soniox ---
    std::thread sender([&] {
        while (true) {
            std::vector<uint8_t> chunk;
            {
                std::unique_lock<std::mutex> lk(capture_state.mtx);
                capture_state.cv.wait(lk, [&] {
                    return !capture_state.queue.empty() || capture_state.stopped;
                });
                if (capture_state.queue.empty()) break; // drained after stop
                chunk = std::move(capture_state.queue.front());
                capture_state.queue.pop_front();
            }
            try { client.sendAudio(chunk); }
            catch (...) { break; } // socket closed (e.g. server error), stop sending
        }
        // Only send end-of-audio if the session didn't end with an error
        if (!session_error.load(std::memory_order_relaxed)) {
            try { client.sendEndOfAudio(); } catch (...) {}
        }
    });

    // --- Receiver thread: pump the WebSocket and fire callbacks while recording ---
    // Must run concurrently with capture so tokens are delivered in real time.
    std::thread receiver([&] {
        try {
            client.run(); // blocks until server sends finished:true
        } catch (const std::exception& e) {
            std::cerr << "\nReceive error: " << e.what() << "\n";
        }
    });

    // --- Start microphone capture ---
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

    // --- Wait for user to press Enter or Ctrl+C ---
    // A detached thread unblocks the main wait loop when Enter is pressed.
    std::thread([] { std::cin.get(); g_stop.store(true, std::memory_order_relaxed); })
        .detach();

    while (!g_stop.load(std::memory_order_relaxed))
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // --- Stop microphone capture ---
    std::cout << "\nStopping microphone...\n";
    ma_device_stop(&device);

    // Signal the sender thread to drain the remaining queue and exit
    {
        std::lock_guard<std::mutex> lk(capture_state.mtx);
        capture_state.stopped = true;
    }
    capture_state.cv.notify_all();
    sender.join();   // waits for sendEndOfAudio() to be sent

    // --- Wait for Soniox to deliver the final transcript ---
    std::cout << "Waiting for final transcript...\n";
    receiver.join(); // waits until server sends finished:true

    ma_device_uninit(&device);
    return 0;
}
