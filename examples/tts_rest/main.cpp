#include <CLI/CLI.hpp>
#include <sonioxpp/soniox.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

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
    std::string outPath     = "tts_output.wav";

    CLI::App app{"Generate speech from text using the Soniox TTS REST API"};
    app.add_option("--text",   text,        "Text to synthesize")->required();
    app.add_option("--lang",   language,    "BCP-47 language code");
    app.add_option("--voice",  voice,       "Voice name (e.g. Adrian, Maya)");
    app.add_option("--format", audioFormat, "Audio output format (e.g. wav, mp3)");
    app.add_option("--out",    outPath,     "Output file path");
    CLI11_PARSE(app, argc, argv);

    soniox::TtsRestClient client(apiKeyEnv);

    soniox::TtsGenerateRequest request;
    request.language     = language;
    request.voice        = voice;
    request.audio_format = audioFormat;
    request.text         = text;

    try {
        const auto response = client.generateSpeech(request);

        std::ofstream out(outPath, std::ios::binary);
        if (!out.is_open()) {
            std::cerr << "Error: cannot open output file: " << outPath << "\n";
            return 1;
        }

        out.write(reinterpret_cast<const char*>(response.body.data()),
                  static_cast<std::streamsize>(response.body.size()));
        out.close();

        std::cout << "Wrote " << response.body.size() << " bytes to " << outPath << "\n";

        if (!response.trailers.empty()) {
            std::cout << "TTS trailers:\n";
            for (const auto& kv : response.trailers)
                std::cout << "  " << kv.first << ": " << kv.second << "\n";
        }
    } catch (const std::exception& ex) {
        std::cerr << "TTS REST failed: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
