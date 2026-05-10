/*
 * Stream an audio file to the Soniox real-time STT WebSocket.
 * Chunks are sent with a configurable delay to simulate a live feed.
 * Non-final tokens update a live preview line; final tokens build the running transcript.
 *
 * Usage:
 *   export SONIOX_API_KEY=<key>
 *   ./soniox_realtime <file> [options]
 *
 * Options:
 *   --format <fmt>   audio format hint (default: auto)
 *   --chunk-ms <ms>  inter-chunk delay in milliseconds (default: 120)
 *   --lang <code>    language hints, comma-separated (default: en)
 *   --diarize        enable speaker diarization
 *   --lang-id        tag each token with its detected language
 *   --debug          verbose logging
 */

#include <CLI/CLI.hpp>
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

int main(int argc, char* argv[])
{
    const char* api_key_env = std::getenv("SONIOX_API_KEY");
    if (!api_key_env) {
        std::cerr << "Error: SONIOX_API_KEY environment variable is not set.\n";
        return 1;
    }

    std::string              audio_path;
    std::string              audio_fmt = soniox::stt::audio_formats::auto_detect;
    int                      chunk_ms  = 120;
    std::vector<std::string> langs{"en"};
    bool                     diarize   = false;
    bool                     lang_id   = false;
    bool                     debug     = false;

    CLI::App app{"Stream an audio file to Soniox real-time STT"};
    app.add_option("file", audio_path, "Audio file to stream")->required();
    app.add_option("--format",   audio_fmt, "Audio format hint (default: auto)");
    app.add_option("--chunk-ms", chunk_ms,  "Chunk delay in milliseconds");
    app.add_option("--lang",     langs,     "Language hints (comma-separated, e.g. en,es)")->delimiter(',');
    app.add_flag("--diarize",  diarize,  "Enable speaker diarization");
    app.add_flag("--lang-id",  lang_id,  "Enable per-token language identification");
    app.add_flag("--debug",    debug,    "Enable debug logging");
    CLI11_PARSE(app, argc, argv);

    if (debug) spdlog::set_level(spdlog::level::debug);

    soniox::RealtimeConfig config;
    config.api_key                        = api_key_env;
    config.model                          = soniox::stt::models::realtime_v4;
    config.audio_format                   = audio_fmt;
    config.language_hints                 = langs;
    config.enable_speaker_diarization     = diarize;
    config.enable_language_identification = lang_id;
    config.enable_endpoint_detection      = true;

    std::string transcript;
    std::mutex  print_mtx;

    soniox::RealtimeClient client;

    client.setOnTokens([&](const std::vector<soniox::Token>& tokens, bool has_final) {
        std::lock_guard<std::mutex> lk(print_mtx);

        if (has_final) {
            for (auto& tok : tokens)
                if (tok.is_final) transcript += tok.text;
        }

        std::string preview = transcript;
        if (!has_final)
            for (auto& tok : tokens) preview += tok.text;

        std::cout << "\r\033[K";
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

    try {
        std::cout << "Connecting to Soniox real-time API...\n";
        client.connect(config);
        std::cout << "Connected. Streaming " << audio_path << "\n";
    }
    catch (const std::exception& e) {
        std::cerr << "Connection failed: " << e.what() << "\n";
        return 1;
    }

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

    try {
        client.run();
    }
    catch (const std::exception& e) {
        std::cerr << "\nReceive error: " << e.what() << "\n";
    }

    sender.join();
    return 0;
}
