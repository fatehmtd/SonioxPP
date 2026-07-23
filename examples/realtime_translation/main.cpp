/*
 * Real-time transcription with one-way translation.
 * Original and translated tokens are printed on two separate live lines.
 *
 * Usage:
 *   export SONIOX_API_KEY=<key>
 *   ./soniox_realtime_translation <file> [options]
 *
 * Options:
 *   --src-lang <code>   source language hint (default: en)
 *   --tgt-lang <code>   translation target language (default: es)
 *   --format <fmt>      audio format hint (default: auto)
 *   --chunk-ms <ms>     inter-chunk delay in milliseconds (default: 120)
 *   --debug             verbose logging
 */

#include <CLI/CLI.hpp>
#include <sonioxpp/soniox.hpp>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

/// Result of parsing one raw JSON text frame from the realtime STT WebSocket.
struct ParsedMessage {
    std::vector<soniox::Token> tokens;
    bool has_final{false};
    bool finished{false};
    std::string error_message;
    int error_code{0};
};

ParsedMessage parseRealtimeMessage(const std::string& raw)
{
    ParsedMessage result;

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(raw);
    }
    catch (const nlohmann::json::exception&) {
        return result;
    }

    result.error_message = payload.value("error_message", std::string());
    if (!result.error_message.empty()) {
        result.error_code = payload.value("error_code", 0);
        return result;
    }

    if (payload.contains("tokens") && payload["tokens"].is_array()) {
        for (const auto& item : payload["tokens"]) {
            soniox::Token token;
            token.text = item.value("text", std::string());
            token.is_final = item.value("is_final", false);
            token.start_ms = item.value("start_ms", 0);
            token.end_ms = item.value("end_ms", 0);
            token.duration_ms = (token.end_ms >= token.start_ms) ? (token.end_ms - token.start_ms) : 0;
            token.confidence = item.value("confidence", 0.0);

            const auto tsIt = item.find("translation_status");
            if (tsIt != item.end() && !tsIt->is_null()) {
                token.translation_status = tsIt->get<std::string>();
            }

            if (token.is_final) {
                result.has_final = true;
            }
            result.tokens.push_back(std::move(token));
        }
    }

    result.finished = payload.value("finished", false);
    return result;
}

} // namespace

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

    soniox::SttRealtimeConfig config;
    config.api_key                  = api_key_env;
    config.model                    = soniox::stt::models::realtime_v5;
    config.audio_format             = audio_fmt;
    config.language_hints           = {src_lang};
    config.enable_endpoint_detection = true;
    config.translation_json = nlohmann::json{
        {"type", soniox::stt::translation_types::one_way},
        {"target_language", tgt_lang},
    }.dump();

    std::string orig_line;
    std::string tran_line;
    std::mutex  print_mtx;

    std::atomic<bool>      done{false};
    std::mutex             done_mtx;
    std::condition_variable done_cv;

    soniox::SttRealtimeClient client;

    client.setOnMessage([&](const std::string& raw) {
        auto parsed = parseRealtimeMessage(raw);

        if (!parsed.error_message.empty()) {
            {
                std::lock_guard<std::mutex> lk(print_mtx);
                std::cerr << "\n[Error " << parsed.error_code << "] " << parsed.error_message << "\n";
            }
            done.store(true);
            done_cv.notify_all();
            return;
        }

        if (!parsed.tokens.empty()) {
            std::lock_guard<std::mutex> lk(print_mtx);

            if (parsed.has_final) {
                for (auto& tok : parsed.tokens) {
                    if (!tok.is_final) continue;
                    if (tok.translation_status == "translated")
                        tran_line += tok.text;
                    else
                        orig_line += tok.text;
                }
            }

            std::string orig_preview = orig_line;
            std::string tran_preview = tran_line;
            if (!parsed.has_final) {
                for (auto& tok : parsed.tokens) {
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
        }

        if (parsed.finished) {
            {
                std::lock_guard<std::mutex> lk(print_mtx);
                std::cout << "\n=== Original ===\n"    << orig_line
                          << "\n=== Translation ===\n" << tran_line << "\n";
            }
            done.store(true);
            done_cv.notify_all();
        }
    });

    client.setOnClosed([&] {
        done.store(true);
        done_cv.notify_all();
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
            std::vector<uint8_t> chunk(buf.begin(), buf.begin() + f.gcount());
            client.sendAudio(chunk);
            std::this_thread::sleep_for(std::chrono::milliseconds(chunk_ms));
        }
        client.sendEndOfAudio();
    });

    {
        std::unique_lock<std::mutex> lock(done_mtx);
        done_cv.wait(lock, [&] { return done.load(); });
    }

    sender.join();
    return 0;
}
