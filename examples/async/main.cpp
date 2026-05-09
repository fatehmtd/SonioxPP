/**
 * @file main.cpp
 * @brief Soniox async transcription example.
 *
 * Uploads a local audio file (or uses a public URL) and transcribes it using
 * Soniox's async REST API.  Polls until the job completes, then prints the
 * full transcript with optional speaker labels.
 *
 * Usage — local file:
 *   export SONIOX_API_KEY=<your_key>
 *   ./soniox_async <audio_file.wav> [options]
 *
 * Usage — public URL:
 *   ./soniox_async --url <https://...> [options]
 *
 * Options:
 *   --lang <code>   Comma-separated language hints (default: en)
 *   --diarize       Enable speaker diarization
 *   --lang-id       Enable per-token language identification
 *   --poll-ms <ms>  Polling interval in ms (default: 1000)
 *   --no-cleanup    Keep the transcription job and file after completion
 *   --debug         Enable debug logging
 */

#include <CLI/CLI.hpp>
#include <sonioxpp/soniox.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

static void renderTranscript(const std::vector<soniox::Token>& tokens, bool diarize)
{
    std::cout << "\n=== Transcript ===\n";

    int current_speaker = -1;
    for (auto& tok : tokens) {
        if (tok.text.empty()) continue;

        if (diarize && tok.speaker != current_speaker) {
            if (current_speaker != -1) std::cout << '\n';
            current_speaker = tok.speaker;
            std::cout << "[Speaker " << current_speaker << "] ";
        }
        std::cout << tok.text;
    }
    std::cout << "\n==================\n";
}

int main(int argc, char* argv[])
{
    const char* api_key_env = std::getenv("SONIOX_API_KEY");
    if (!api_key_env) {
        std::cerr << "Error: SONIOX_API_KEY environment variable is not set.\n";
        return 1;
    }

    std::string              audio_path;
    std::string              audio_url;
    std::vector<std::string> langs{"en"};
    bool                     diarize    = false;
    bool                     lang_id    = false;
    bool                     no_cleanup = false;
    int                      poll_ms    = 1000;
    bool                     debug      = false;

    CLI::App app{"Transcribe audio via the Soniox async REST API"};
    app.add_option("file",       audio_path, "Local audio file to transcribe");
    app.add_option("--url",      audio_url,  "Public HTTPS URL of the audio file");
    app.add_option("--lang",     langs,      "Language hints (comma-separated, e.g. en,es)")->delimiter(',');
    app.add_option("--poll-ms",  poll_ms,    "Polling interval in milliseconds");
    app.add_flag("--diarize",    diarize,    "Enable speaker diarization");
    app.add_flag("--lang-id",    lang_id,    "Enable per-token language identification");
    app.add_flag("--no-cleanup", no_cleanup, "Keep job and file after completion");
    app.add_flag("--debug",      debug,      "Enable debug logging");
    CLI11_PARSE(app, argc, argv);

    if (audio_path.empty() && audio_url.empty()) {
        std::cerr << "Error: provide an audio file or --url <url>.\n" << app.help();
        return 1;
    }

    if (debug) spdlog::set_level(spdlog::level::debug);
    bool cleanup = !no_cleanup;

    soniox::AsyncConfig config;
    config.api_key                        = api_key_env;
    config.model                          = soniox::stt::models::async_v4;
    config.language_hints                 = langs;
    config.enable_speaker_diarization     = diarize;
    config.enable_language_identification = lang_id;

    soniox::AsyncClient client(api_key_env);

    try {
        if (!audio_path.empty()) {
            std::cout << "Uploading and transcribing: " << audio_path << "\n";
            auto tokens = client.transcribeFile(audio_path, config, poll_ms);
            renderTranscript(tokens, diarize);

        } else {
            config.audio_url = audio_url;
            std::cout << "Creating transcription from URL: " << audio_url << "\n";

            std::string tx_id = client.createTranscription(config);
            std::cout << "Job ID: " << tx_id << "\nPolling";

            while (true) {
                std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
                std::cout << '.' << std::flush;

                auto tx = client.getTranscription(tx_id);

                if (tx.status == soniox::TranscriptionStatus::Completed) {
                    std::cout << " done.\n";
                    auto tokens = client.getTranscript(tx_id);
                    if (cleanup) client.deleteTranscription(tx_id);
                    renderTranscript(tokens, diarize);
                    break;
                }

                if (tx.status == soniox::TranscriptionStatus::Error) {
                    std::cout << '\n';
                    std::cerr << "Transcription failed: " << tx.error_message << "\n";
                    if (cleanup) client.deleteTranscription(tx_id);
                    return 1;
                }
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
