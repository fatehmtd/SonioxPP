#include <sonioxpp/soniox.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> base64Decode(const std::string& input)
{
    static const int decodeTable[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    std::vector<std::uint8_t> output;
    output.reserve((input.size() * 3) / 4);

    int val = 0;
    int valb = -8;
    for (const unsigned char c : input) {
        if (c == '=') {
            break;
        }
        const int d = decodeTable[c];
        if (d == -1) {
            continue;
        }
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            output.push_back(static_cast<std::uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return output;
}

void printUsage(const char* prog)
{
    std::cerr
        << "Usage:\n"
        << "  " << prog << " --text <text> [--lang en] [--voice Adrian] [--format wav] [--out out.wav]\n";
}

} // namespace

int main(int argc, char* argv[])
{
    const char* apiKeyEnv = std::getenv("SONIOX_API_KEY");
    if (apiKeyEnv == nullptr) {
        std::cerr << "Error: SONIOX_API_KEY is not set\n";
        return 1;
    }

    std::string text;
    std::string language = "en";
    std::string voice = "Adrian";
    std::string audioFormat = "wav";
    std::string outPath = "tts_realtime_output.wav";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--text" && i + 1 < argc) {
            text = argv[++i];
        } else if (arg == "--lang" && i + 1 < argc) {
            language = argv[++i];
        } else if (arg == "--voice" && i + 1 < argc) {
            voice = argv[++i];
        } else if (arg == "--format" && i + 1 < argc) {
            audioFormat = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            outPath = argv[++i];
        }
    }

    if (text.empty()) {
        printUsage(argv[0]);
        return 1;
    }

    std::atomic<bool> done{false};
    std::mutex doneMutex;
    std::condition_variable doneCv;

    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Error: cannot open output file: " << outPath << "\n";
        return 1;
    }

    soniox::TtsRealtimeClient client;

    client.setOnParsedMessage([&](const soniox::TtsRealtimeMessage& message) {
        if (!message.audio.empty()) {
            auto bytes = base64Decode(message.audio);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        if (message.terminated) {
            {
                std::lock_guard<std::mutex> lk(doneMutex);
                done.store(true);
            }
            doneCv.notify_all();
        }

        if (message.error_code != 0 || !message.error_message.empty()) {
            std::cerr << "TTS stream error " << message.error_code << ": " << message.error_message << "\n";
        }
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
        cfg.api_key = apiKeyEnv;
        cfg.stream_id = "stream-001";
        cfg.language = language;
        cfg.voice = voice;
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
