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

#include <sonioxpp/soniox.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void printUsage(const char* prog)
{
    std::cerr
        << "Usage:\n"
        << "  " << prog << " <audio_file>   [options]\n"
        << "  " << prog << " --url <url>    [options]\n\n"
        << "Options:\n"
        << "  --lang <code>    Comma-separated language hints (default: en)\n"
        << "  --diarize        Enable speaker diarization\n"
        << "  --lang-id        Enable per-token language identification\n"
        << "  --poll-ms <ms>   Polling interval (default: 1000)\n"
        << "  --no-cleanup     Keep job and file after completion\n"
        << "  --debug          Enable debug logging\n";
}

static std::vector<std::string> splitLangs(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else           cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ---------------------------------------------------------------------------
// Transcript rendering
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    // --- API key ---
    const char* api_key_env = std::getenv("SONIOX_API_KEY");
    if (!api_key_env) {
        std::cerr << "Error: SONIOX_API_KEY environment variable is not set.\n";
        return 1;
    }

    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    // --- Parse arguments ---
    std::string audio_path;
    std::string audio_url;
    std::string lang_str  = "en";
    bool        diarize   = false;
    bool        lang_id   = false;
    bool        cleanup   = true;
    int         poll_ms   = 1000;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if      (arg == "--url"      && i + 1 < argc) audio_url  = argv[++i];
        else if (arg == "--lang"     && i + 1 < argc) lang_str   = argv[++i];
        else if (arg == "--poll-ms"  && i + 1 < argc) poll_ms    = std::stoi(argv[++i]);
        else if (arg == "--diarize")                  diarize    = true;
        else if (arg == "--lang-id")                  lang_id    = true;
        else if (arg == "--no-cleanup")               cleanup    = false;
        else if (arg == "--debug")
            spdlog::set_level(spdlog::level::debug);
        else if (arg[0] != '-')
            audio_path = arg;
    }

    if (audio_path.empty() && audio_url.empty()) {
        std::cerr << "Error: provide an audio file or --url <url>.\n";
        printUsage(argv[0]);
        return 1;
    }

    // --- Build config ---
    soniox::AsyncConfig config;
    config.api_key                        = api_key_env;
    config.model                          = "stt-async-v4";
    config.language_hints                 = splitLangs(lang_str);
    config.enable_speaker_diarization     = diarize;
    config.enable_language_identification = lang_id;

    soniox::AsyncClient client(api_key_env);

    try {
        if (!audio_path.empty()) {
            // --- Convenience path: single call handles everything ---
            std::cout << "Uploading and transcribing: " << audio_path << "\n";
            auto tokens = client.transcribeFile(audio_path, config, poll_ms);
            renderTranscript(tokens, diarize);

        } else {
            // --- Step-by-step path: useful for URLs or manual control ---
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
