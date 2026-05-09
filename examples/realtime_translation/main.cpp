/**
 * @file main.cpp
 * @brief Soniox real-time translation example.
 *
 * Streams an audio file to the Soniox WebSocket endpoint with one-way
 * translation enabled.  Incoming tokens are separated by translation_status
 * and printed on two independent running lines: one for the original speech
 * and one for the translated output.
 *
 * Usage:
 *   export SONIOX_API_KEY=<your_key>
 *   ./soniox_realtime_translation <audio_file> [options]
 *
 * Options:
 *   --src-lang <code>   Source language BCP-47 hint (default: en)
 *   --tgt-lang <code>   Target translation language (default: es)
 *   --format <fmt>      Audio format hint (default: auto)
 *   --chunk-ms <ms>     Simulated chunk delay in ms (default: 120)
 *   --debug             Enable debug logging
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

    std::string audio_path;
    std::string audio_fmt = soniox::stt::audio_formats::auto_detect;
    std::string src_lang  = "en";
    std::string tgt_lang  = "es";
    int         chunk_ms  = 120;
    bool        debug     = false;

    CLI::App app{"Stream an audio file to Soniox real-time STT with one-way translation"};
    app.add_option("file",       audio_path, "Audio file to stream")->required();
    app.add_option("--src-lang", src_lang,   "Source language BCP-47 hint");
    app.add_option("--tgt-lang", tgt_lang,   "Target translation language");
    app.add_option("--format",   audio_fmt,  "Audio format hint (default: auto)");
    app.add_option("--chunk-ms", chunk_ms,   "Chunk delay in milliseconds");
    app.add_flag("--debug",      debug,      "Enable debug logging");
    CLI11_PARSE(app, argc, argv);

    if (debug) spdlog::set_level(spdlog::level::debug);

    soniox::RealtimeConfig config;
    config.api_key                  = api_key_env;
    config.model                    = soniox::stt::models::realtime_v4;
    config.audio_format             = audio_fmt;
    config.language_hints           = {src_lang};
    config.enable_endpoint_detection = true;
    config.translation.type         = soniox::stt::translation_types::one_way;
    config.translation.language_a   = tgt_lang;

    std::string orig_line;
    std::string tran_line;
    std::mutex  print_mtx;

    soniox::RealtimeClient client;

    client.setOnTokens([&](const std::vector<soniox::Token>& tokens, bool has_final) {
        std::lock_guard<std::mutex> lk(print_mtx);

        if (has_final) {
            for (auto& tok : tokens) {
                if (!tok.is_final) continue;
                if (tok.translation_status == "translated")
                    tran_line += tok.text;
                else
                    orig_line += tok.text;
            }
        }

        std::string orig_preview = orig_line;
        std::string tran_preview = tran_line;
        if (!has_final) {
            for (auto& tok : tokens) {
                if (tok.translation_status == "translated")
                    tran_preview += tok.text;
                else
                    orig_preview += tok.text;
            }
        }

        std::cout << "\033[2A"
                  << "\r\033[K[orig] " << orig_preview << "\n"
                  << "\r\033[K[tran] " << tran_preview << "\n"
                  << std::flush;
    });

    client.setOnFinished([&] {
        std::lock_guard<std::mutex> lk(print_mtx);
        std::cout << "\n=== Original ===\n"    << orig_line
                  << "\n=== Translation ===\n" << tran_line << "\n";
    });

    client.setOnError([&](const soniox::RealtimeError& err) {
        std::lock_guard<std::mutex> lk(print_mtx);
        std::cerr << "\n[Error " << err.error_code << "] " << err.error_message << "\n";
    });

    try {
        std::cout << "Connecting (src=" << src_lang << " → tgt=" << tgt_lang << ")...\n";
        client.connect(config);
        std::cout << "Connected. Streaming " << audio_path << "\n";
    }
    catch (const std::exception& e) {
        std::cerr << "Connection failed: " << e.what() << "\n";
        return 1;
    }

    std::cout << "\n\n" << std::flush;

    static constexpr size_t CHUNK_BYTES = 4096;

    std::thread sender([&] {
        std::ifstream f(audio_path, std::ios::binary);
        if (!f.is_open()) {
            std::cerr << "\nCannot open audio file: " << audio_path << "\n";
            client.sendEndOfAudio();
            return;
        }
        std::vector<uint8_t> buf(CHUNK_BYTES);
        while (f.read(reinterpret_cast<char*>(buf.data()), CHUNK_BYTES) || f.gcount() > 0) {
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
