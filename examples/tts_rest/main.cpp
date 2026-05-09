#include <sonioxpp/soniox.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

static void printUsage(const char* prog)
{
    std::cerr
        << "Usage:\n"
        << "  " << prog << " --text <text> [--lang en] [--voice Adrian] [--format wav] [--out out.wav]\n";
}

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
    std::string outPath = "tts_output.wav";

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

    soniox::TtsRestClient client(apiKeyEnv);

    soniox::TtsGenerateRequest request;
    request.language = language;
    request.voice = voice;
    request.audio_format = audioFormat;
    request.text = text;

    try {
        const auto response = client.generateSpeech(request);

        std::ofstream out(outPath, std::ios::binary);
        if (!out.is_open()) {
            std::cerr << "Error: cannot open output file: " << outPath << "\n";
            return 1;
        }

        out.write(reinterpret_cast<const char*>(response.body.data()), static_cast<std::streamsize>(response.body.size()));
        out.close();

        std::cout << "Wrote " << response.body.size() << " bytes to " << outPath << "\n";

        if (!response.trailers.empty()) {
            std::cout << "TTS trailers:\n";
            for (const auto& kv : response.trailers) {
                std::cout << "  " << kv.first << ": " << kv.second << "\n";
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "TTS REST failed: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
