/**
 * @file main.cpp
 * @brief Soniox TTS WebSocket multiplexing example.
 *
 * Opens a single TTS WebSocket connection and starts three concurrent streams,
 * each synthesising a different text with a different voice and language.
 * Audio for each stream is written to a separate output file.  The program
 * waits until all streams have terminated before closing the connection.
 *
 * Demonstrates that up to five streams may be active simultaneously on one
 * TTS WebSocket connection, with audio chunks routed by stream_id.
 *
 * Usage:
 *   export SONIOX_API_KEY=<your_key>
 *   ./soniox_tts_multiplex [--format wav] [--debug]
 */

#include <CLI/CLI.hpp>
#include <sonioxpp/soniox.hpp>
#include <spdlog/spdlog.h>

#include <cppcodec/base64_rfc4648.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct StreamSpec {
    std::string stream_id;
    std::string language;
    std::string voice;
    std::string text;
    std::string out_path;
};

} // namespace

int main(int argc, char* argv[])
{
    const char* api_key_env = std::getenv("SONIOX_API_KEY");
    if (!api_key_env) {
        std::cerr << "Error: SONIOX_API_KEY is not set\n";
        return 1;
    }

    std::string audio_format = soniox::tts::audio_formats::wav;
    bool        debug        = false;

    CLI::App app{"Synthesize multiple texts concurrently over one TTS WebSocket connection"};
    app.add_option("--format", audio_format, "Audio output format (e.g. wav, mp3)");
    app.add_flag("--debug",    debug,        "Enable debug logging");
    CLI11_PARSE(app, argc, argv);

    if (debug) spdlog::set_level(spdlog::level::debug);

    const std::vector<StreamSpec> streams = {
        {"s1", "en", "Adrian", "Hello from stream one. This is the English voice.",          "multiplex_s1." + audio_format},
        {"s2", "es", "Maya",   "Hola desde el flujo dos. Esta es la voz en español.",        "multiplex_s2." + audio_format},
        {"s3", "fr", "Claire", "Bonjour depuis le flux trois. C'est la voix en français.",   "multiplex_s3." + audio_format},
    };

    const int total_streams = static_cast<int>(streams.size());

    std::map<std::string, std::ofstream> out_files;
    for (auto& spec : streams) {
        out_files[spec.stream_id].open(spec.out_path, std::ios::binary);
        if (!out_files[spec.stream_id].is_open()) {
            std::cerr << "Cannot open output file: " << spec.out_path << "\n";
            return 1;
        }
    }

    std::atomic<int>        streams_done{0};
    std::mutex              state_mtx;
    std::condition_variable done_cv;

    soniox::TtsRealtimeClient client;

    client.setOnParsedMessage([&](const soniox::TtsRealtimeMessage& msg) {
        std::lock_guard<std::mutex> lk(state_mtx);

        if (!msg.audio.empty()) {
            auto bytes = cppcodec::base64_rfc4648::decode(msg.audio);
            auto it = out_files.find(msg.stream_id);
            if (it != out_files.end() && it->second.is_open()) {
                it->second.write(
                    reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
            }
        }

        if (msg.error_code != 0) {
            std::cerr << "[" << msg.stream_id << "] stream error "
                      << msg.error_code << ": " << msg.error_message << "\n";
        }

        if (msg.terminated) {
            std::cout << "[" << msg.stream_id << "] terminated\n";
            auto it = out_files.find(msg.stream_id);
            if (it != out_files.end())
                it->second.close();

            if (++streams_done >= total_streams)
                done_cv.notify_all();
        }
    });

    client.setOnError([&](const std::string& err) {
        std::cerr << "WebSocket error: " << err << "\n";
        done_cv.notify_all();
    });

    client.setOnClosed([&] {
        done_cv.notify_all();
    });

    try {
        client.connect();

        for (auto& spec : streams) {
            soniox::TtsRealtimeStreamConfig cfg;
            cfg.api_key      = api_key_env;
            cfg.stream_id    = spec.stream_id;
            cfg.language     = spec.language;
            cfg.voice        = spec.voice;
            cfg.audio_format = audio_format;

            client.startStream(cfg);
            client.sendText(spec.stream_id, spec.text, true);
            std::cout << "Started stream " << spec.stream_id
                      << " (" << spec.voice << "/" << spec.language << ")\n";
        }

        std::unique_lock<std::mutex> lk(state_mtx);
        done_cv.wait(lk, [&] { return streams_done.load() >= total_streams; });
        lk.unlock();

        client.close();

        for (auto& spec : streams)
            std::cout << "Wrote " << spec.out_path << "\n";

    } catch (const std::exception& ex) {
        std::cerr << "TTS multiplex failed: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
