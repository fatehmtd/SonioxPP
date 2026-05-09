#include <CLI/CLI.hpp>
#include <sonioxpp/soniox.hpp>

#include <cppcodec/base64_rfc4648.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
    const char* apiKeyEnv = std::getenv("SONIOX_API_KEY");
    if (apiKeyEnv == nullptr) {
        std::cerr << "Error: SONIOX_API_KEY is not set\n";
        return 1;
    }

    std::string text;
    std::string language    = "en";
    std::string voice       = "Adrian";
    std::string audioFormat = "wav";
    std::string outPath     = "tts_realtime_output.wav";

    CLI::App app{"Generate speech from text using the Soniox TTS WebSocket API"};
    app.add_option("--text",   text,        "Text to synthesize")->required();
    app.add_option("--lang",   language,    "BCP-47 language code");
    app.add_option("--voice",  voice,       "Voice name (e.g. Adrian, Maya)");
    app.add_option("--format", audioFormat, "Audio output format (e.g. wav, mp3)");
    app.add_option("--out",    outPath,     "Output file path");
    CLI11_PARSE(app, argc, argv);

    std::atomic<bool>       done{false};
    std::mutex              doneMutex;
    std::condition_variable doneCv;

    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Error: cannot open output file: " << outPath << "\n";
        return 1;
    }

    soniox::TtsRealtimeClient client;

    client.setOnParsedMessage([&](const soniox::TtsRealtimeMessage& message) {
        if (!message.audio.empty()) {
            auto bytes = cppcodec::base64_rfc4648::decode(message.audio);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }

        if (message.terminated) {
            {
                std::lock_guard<std::mutex> lk(doneMutex);
                done.store(true);
            }
            doneCv.notify_all();
        }

        if (message.error_code != 0 || !message.error_message.empty())
            std::cerr << "TTS stream error " << message.error_code
                      << ": " << message.error_message << "\n";
    });

    client.setOnError([&](const std::string& err) {
        std::cerr << "WebSocket error: " << err << "\n";
        {
            std::lock_guard<std::mutex> lk(doneMutex);
            done.store(true);
        }
        doneCv.notify_all();
    });

    client.setOnClosed([&] {
        {
            std::lock_guard<std::mutex> lk(doneMutex);
            done.store(true);
        }
        doneCv.notify_all();
    });

    try {
        client.connect();

        soniox::TtsRealtimeStreamConfig cfg;
        cfg.api_key      = apiKeyEnv;
        cfg.stream_id    = "stream-001";
        cfg.language     = language;
        cfg.voice        = voice;
        cfg.audio_format = audioFormat;

        client.startStream(cfg);
        client.sendText(cfg.stream_id, text, true);

        std::unique_lock<std::mutex> lock(doneMutex);
        doneCv.wait(lock, [&] { return done.load(); });

        client.close();
        out.close();

        std::cout << "Wrote realtime TTS output to " << outPath << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "TTS realtime failed: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
