/**
 * @file main.cpp
 * @brief Soniox real-time streaming example.
 *
 * Reads an audio file from disk and streams it to Soniox's WebSocket
 * endpoint in fixed-size chunks with a configurable inter-chunk delay,
 * simulating a live microphone feed.  Transcription tokens are printed
 * as they arrive; a live preview line is updated in-place and final
 * tokens are committed to the running transcript.
 *
 * Usage:
 *   export SONIOX_API_KEY=<your_key>
 *   ./soniox_realtime <audio_file> [--format auto] [--chunk-ms 120]
 *
 * Supported audio formats: wav, pcm_s16le, mp3, ogg, flac (pass "auto"
 * to let the server detect the format automatically).
 */

#include <sonioxpp/soniox.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

 // ---------------------------------------------------------------------------
 // Helpers
 // ---------------------------------------------------------------------------

static void printUsage(const char* prog)
{
    std::cerr
        << "Usage:\n"
        << "  " << prog << " <audio_file> [options]\n\n"
        << "Options:\n"
        << "  --format <fmt>    Audio format hint (default: auto)\n"
        << "  --chunk-ms <ms>   Chunk delay in milliseconds (default: 120)\n"
        << "  --lang <code>     Comma-separated language hints (default: en)\n"
        << "  --diarize         Enable speaker diarization\n"
        << "  --lang-id         Enable per-token language identification\n"
        << "  --debug           Enable debug logging\n";
}

// Split "en,es" -> {"en", "es"}
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
    // --- API key ---
    const char* api_key_env = std::getenv("SONIOX_API_KEY");
    if (!api_key_env) {
        std::cerr << "Error: SONIOX_API_KEY environment variable is not set.\n";
        return 1;
    }

    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    // --- Parse arguments ---
    std::string audio_path = argv[1];
    std::string audio_fmt = "auto";
    int         chunk_ms = 120;
    std::string lang_str = "en";
    bool        diarize = false;
    bool        lang_id = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--format" && i + 1 < argc) audio_fmt = argv[++i];
        else if (arg == "--chunk-ms" && i + 1 < argc) chunk_ms = std::stoi(argv[++i]);
        else if (arg == "--lang" && i + 1 < argc) lang_str = argv[++i];
        else if (arg == "--diarize")                  diarize = true;
        else if (arg == "--lang-id")                  lang_id = true;
        else if (arg == "--debug")
            spdlog::set_level(spdlog::level::debug);
    }

    // --- Build config ---
    soniox::RealtimeConfig config;
    config.api_key = api_key_env;
    config.model = "stt-rt-v4";
    config.audio_format = audio_fmt;
    config.language_hints = splitLangs(lang_str);
    config.enable_speaker_diarization = diarize;
    config.enable_language_identification = lang_id;
    config.enable_endpoint_detection = true;

    // --- State ---
    std::string transcript;   // accumulates final tokens
    std::mutex  print_mtx;

    // --- Client callbacks ---
    soniox::RealtimeClient client;

    client.setOnTokens([&](const std::vector<soniox::Token>& tokens, bool has_final) {
        std::lock_guard<std::mutex> lk(print_mtx);

        if (has_final) {
            for (auto& tok : tokens)
                if (tok.is_final) transcript += tok.text;
        }

        // Build live preview: final transcript + current non-final tail
        std::string preview = transcript;
        if (!has_final)
            for (auto& tok : tokens) preview += tok.text;

        // Overwrite current line
        std::cout << "\r\033[K";   // carriage-return + erase line
        std::cout << (has_final ? "[F] " : "[~] ") << preview << std::flush;
        if (has_final) std::cout << std::endl << std::endl;
        });

    client.setOnFinished([&] {
        std::lock_guard<std::mutex> lk(print_mtx);
        std::cout << "\n\n=== Session complete ===\n" << transcript << "\n";
        });

    client.setOnError([&](const soniox::RealtimeError& err) {
        std::lock_guard<std::mutex> lk(print_mtx);
        std::cerr << "\n[Error " << err.error_code << "] " << err.error_message << "\n";
        });

    // --- Connect ---
    try {
        std::cout << "Connecting to Soniox real-time API...\n";
        client.connect(config);
        std::cout << "Connected. Streaming " << audio_path << "\n";
    }
    catch (const std::exception& e) {
        std::cerr << "Connection failed: " << e.what() << "\n";
        return 1;
    }

    // --- Stream audio in a background thread ---
    static constexpr size_t CHUNK_BYTES = 4096;

    std::thread sender([&] {
        std::ifstream f(audio_path, std::ios::binary);
        if (!f.is_open()) {
            std::cerr << "\nCannot open audio file: " << audio_path << "\n";
            client.sendEndOfAudio();
            return;
        }

        std::vector<uint8_t> buf(CHUNK_BYTES);
        while (f.read(reinterpret_cast<char*>(buf.data()), CHUNK_BYTES) ||
            f.gcount() > 0)
        {
            client.sendAudio(buf.data(), static_cast<size_t>(f.gcount()));
            std::this_thread::sleep_for(std::chrono::milliseconds(chunk_ms));
        }

        client.sendEndOfAudio();
        });

    // --- Receive loop (blocks until finished or error) ---
    try {
        client.run();
    }
    catch (const std::exception& e) {
        std::cerr << "\nReceive error: " << e.what() << "\n";
    }

    sender.join();
    return 0;
}
