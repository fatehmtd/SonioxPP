/**
 * @file main.cpp
 * @brief Soniox incremental TTS text-streaming example.
 *
 * Reads text from a file or stdin, splits it into individual words, and
 * sends them one at a time to the TTS WebSocket with a configurable inter-word
 * delay.  All calls except the last use sendText(..., false); the final word
 * uses sendText(..., true).  This simulates the token-by-token output of an
 * LLM and demonstrates that audio synthesis begins before all text has arrived.
 *
 * Time-to-first-audio (TTFA) and total synthesis time are reported on exit.
 *
 * Usage:
 *   export SONIOX_API_KEY=<your_key>
 *   ./soniox_tts_streaming_text --file <text_file> [options]
 *   echo "Hello world" | ./soniox_tts_streaming_text [options]
 *
 * Options:
 *   --file <path>          Read text from file instead of stdin
 *   --lang <code>          BCP-47 language code (default: en)
 *   --voice <name>         Voice name (default: Adrian)
 *   --format <fmt>         Audio output format (default: wav)
 *   --out <path>           Output audio file path (default: tts_streaming_output.wav)
 *   --word-delay-ms <ms>   Delay between sending each word (default: 80)
 *   --debug                Enable debug logging
 */

#include <CLI/CLI.hpp>
#include <sonioxpp/soniox.hpp>
#include <spdlog/spdlog.h>

#include <cppcodec/base64_rfc4648.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static std::vector<std::string> splitWords(const std::string& text)
{
    std::vector<std::string> words;
    std::istringstream ss(text);
    std::string word;
    while (ss >> word)
        words.push_back(word);
    return words;
}

int main(int argc, char* argv[])
{
    const char* api_key_env = std::getenv("SONIOX_API_KEY");
    if (!api_key_env) {
        std::cerr << "Error: SONIOX_API_KEY is not set\n";
        return 1;
    }

    std::string file_path;
    std::string language      = "en";
    std::string voice         = "Adrian";
    std::string audio_format  = soniox::tts::audio_formats::wav;
    std::string out_path      = "tts_streaming_output.wav";
    int         word_delay_ms = 80;
    bool        debug         = false;

    CLI::App app{"Stream text word-by-word to the Soniox TTS WebSocket (simulates LLM output)"};
    app.add_option("--file",          file_path,    "Read text from file instead of stdin");
    app.add_option("--lang",          language,     "BCP-47 language code");
    app.add_option("--voice",         voice,        "Voice name (e.g. Adrian, Maya)");
    app.add_option("--format",        audio_format, "Audio output format (e.g. wav, mp3)");
    app.add_option("--out",           out_path,     "Output audio file path");
    app.add_option("--word-delay-ms", word_delay_ms,"Delay between sending each word in ms");
    app.add_flag("--debug",           debug,        "Enable debug logging");
    CLI11_PARSE(app, argc, argv);

    if (debug) spdlog::set_level(spdlog::level::debug);

    std::string text;
    if (!file_path.empty()) {
        std::ifstream f(file_path);
        if (!f.is_open()) {
            std::cerr << "Error: cannot open file: " << file_path << "\n";
            return 1;
        }
        text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    } else {
        text.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    }

    auto words = splitWords(text);
    if (words.empty()) {
        std::cerr << "Error: no text to synthesize.\n";
        return 1;
    }

    std::cout << "Synthesizing " << words.size() << " word(s) with "
              << word_delay_ms << " ms inter-word delay...\n";

    std::ofstream out_file(out_path, std::ios::binary);
    if (!out_file.is_open()) {
        std::cerr << "Error: cannot open output file: " << out_path << "\n";
        return 1;
    }

    using Clock = std::chrono::steady_clock;
    Clock::time_point send_start;
    Clock::time_point first_audio_at;
    std::atomic<bool> first_audio_received{false};
    std::mutex        timing_mtx;

    std::atomic<bool>       done{false};
    std::mutex              done_mutex;
    std::condition_variable done_cv;

    soniox::TtsRealtimeClient client;

    client.setOnParsedMessage([&](const soniox::TtsRealtimeMessage& msg) {
        if (!msg.audio.empty()) {
            {
                std::lock_guard<std::mutex> lk(timing_mtx);
                if (!first_audio_received.exchange(true)) {
                    first_audio_at = Clock::now();
                    auto ttfa = std::chrono::duration_cast<std::chrono::milliseconds>(
                        first_audio_at - send_start).count();
                    std::cout << "[TTFA: " << ttfa << " ms]\n" << std::flush;
                }
            }
            auto bytes = cppcodec::base64_rfc4648::decode(msg.audio);
            out_file.write(reinterpret_cast<const char*>(bytes.data()),
                           static_cast<std::streamsize>(bytes.size()));
        }

        if (msg.error_code != 0)
            std::cerr << "Stream error " << msg.error_code << ": "
                      << msg.error_message << "\n";

        if (msg.terminated) {
            {
                std::lock_guard<std::mutex> lk(done_mutex);
                done.store(true);
            }
            done_cv.notify_all();
        }
    });

    client.setOnError([&](const std::string& err) {
        std::cerr << "WebSocket error: " << err << "\n";
        {
            std::lock_guard<std::mutex> lk(done_mutex);
            done.store(true);
        }
        done_cv.notify_all();
    });

    client.setOnClosed([&] {
        {
            std::lock_guard<std::mutex> lk(done_mutex);
            done.store(true);
        }
        done_cv.notify_all();
    });

    try {
        client.connect();

        soniox::TtsRealtimeStreamConfig cfg;
        cfg.api_key      = api_key_env;
        cfg.stream_id    = "stream-text";
        cfg.language     = language;
        cfg.voice        = voice;
        cfg.audio_format = audio_format;

        client.startStream(cfg);
        send_start = Clock::now();

        std::thread sender([&] {
            for (size_t i = 0; i < words.size(); ++i) {
                bool        is_last = (i == words.size() - 1);
                std::string chunk   = (i == 0 ? words[i] : " " + words[i]);
                client.sendText(cfg.stream_id, chunk, is_last);
                std::cout << "[send] " << chunk << (is_last ? " [END]" : "") << "\n";
                if (!is_last)
                    std::this_thread::sleep_for(std::chrono::milliseconds(word_delay_ms));
            }
        });

        std::unique_lock<std::mutex> lk(done_mutex);
        done_cv.wait(lk, [&] { return done.load(); });
        lk.unlock();

        sender.join();
        client.close();
        out_file.close();

        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - send_start).count();
        std::cout << "Total synthesis time: " << total_ms << " ms\n";
        std::cout << "Wrote audio to " << out_path << "\n";

    } catch (const std::exception& ex) {
        std::cerr << "TTS streaming failed: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
