/*
 * Async transcription with per-token timestamps, confidence scores, and speaker labels.
 * Output is either a tab-separated table or SRT subtitles.
 *
 * Usage:
 *   export SONIOX_API_KEY=<key>
 *   ./soniox_async_timestamps <file> [options]
 *   ./soniox_async_timestamps --url <https://...> [options]
 *
 * Options:
 *   --lang <code>       language hints, comma-separated (default: en)
 *   --diarize           enable speaker diarization
 *   --lang-id           tag each token with its detected language
 *   --output srt|table  output format (default: table)
 *   --poll-ms <ms>      polling interval (default: 1000)
 *   --no-cleanup        keep the uploaded file and job after completion
 *   --debug             verbose logging
 */

#include <CLI/CLI.hpp>
#include <sonioxpp/soniox.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

static std::string formatSrtTime(int ms)
{
    int h   = ms / 3600000;
    int m   = (ms % 3600000) / 60000;
    int s   = (ms % 60000) / 1000;
    int rem = ms % 1000;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d,%03d", h, m, s, rem);
    return buf;
}

static void renderSrt(const std::vector<soniox::SttTranscriptToken>& tokens)
{
    std::cout << "\n";
    int  index       = 1;
    int  block_start = -1;
    int  block_end   = 0;
    std::string block_text;

    auto flush = [&] {
        if (block_text.empty()) return;
        std::cout << index++ << "\n"
                  << formatSrtTime(block_start) << " --> "
                  << formatSrtTime(block_end)   << "\n"
                  << block_text                 << "\n\n";
        block_text.clear();
        block_start = -1;
    };

    for (auto& tok : tokens) {
        if (tok.text.empty()) continue;
        if (block_start < 0) block_start = tok.start_ms;
        block_end = tok.end_ms;
        block_text += tok.text;

        bool sentence_end = !block_text.empty() &&
            (block_text.back() == '.' || block_text.back() == '?' || block_text.back() == '!');
        bool too_long = (tok.end_ms - block_start > 5000);
        if (sentence_end || too_long)
            flush();
    }
    flush();
}

static void renderTable(const std::vector<soniox::SttTranscriptToken>& tokens)
{
    std::cout << "\nstart_ms\tend_ms\tconf\tspeaker\tlanguage\ttext\n"
              << std::string(72, '-') << "\n";
    for (auto& tok : tokens) {
        if (tok.text.empty()) continue;
        std::cout << tok.start_ms << "\t"
                  << tok.end_ms   << "\t"
                  << std::fixed << std::setprecision(3) << tok.confidence << "\t"
                  << (tok.speaker.empty()  ? "-" : tok.speaker)  << "\t"
                  << (tok.language.empty() ? "-" : tok.language) << "\t"
                  << tok.text     << "\n";
    }
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
    std::string              output     = "table";
    int                      poll_ms    = 1000;
    bool                     debug      = false;

    CLI::App app{"Transcribe audio and render per-token timestamps via Soniox async REST API"};
    app.add_option("file",       audio_path, "Local audio file to transcribe");
    app.add_option("--url",      audio_url,  "Public HTTPS URL of the audio file");
    app.add_option("--lang",     langs,      "Language hints (comma-separated, e.g. en,es)")->delimiter(',');
    app.add_option("--output",   output,     "Output format: srt or table")
       ->check(CLI::IsMember({"srt", "table"}));
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

    soniox::SttRestClient client(api_key_env);
    std::string file_id;

    try {
        if (!audio_path.empty()) {
            std::cout << "Uploading " << audio_path << "...\n";
            soniox::SttFile f = client.uploadFileTyped(audio_path);
            file_id = f.id;
            std::cout << "File ID: " << file_id << "\n";
        }

        soniox::SttCreateTranscriptionRequest req;
        req.model                          = soniox::stt::models::async_v4;
        req.language_hints                 = langs;
        req.enable_speaker_diarization     = diarize;
        req.enable_language_identification = lang_id;
        if (!file_id.empty())
            req.file_id = file_id;
        else
            req.audio_url = audio_url;

        soniox::SttTranscription tx = client.createTranscriptionTyped(req);
        std::cout << "Job ID: " << tx.id << "\nPolling";

        while (tx.status == "queued" || tx.status == "processing") {
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
            std::cout << '.' << std::flush;
            tx = client.getTranscriptionTyped(tx.id);
        }
        std::cout << " done.\n";

        if (tx.status == "error") {
            std::cerr << "Transcription failed [" << tx.error_type << "]: "
                      << tx.error_message << "\n";
            if (cleanup) {
                try { client.deleteTranscription(tx.id); } catch (...) {}
                if (!file_id.empty())
                    try { client.deleteFile(file_id); } catch (...) {}
            }
            return 1;
        }

        soniox::SttTranscript result = client.getTranscriptionTranscriptTyped(tx.id);

        if (cleanup) {
            client.deleteTranscription(tx.id);
            if (!file_id.empty())
                client.deleteFile(file_id);
        }

        if (output == "srt")
            renderSrt(result.tokens);
        else
            renderTable(result.tokens);

    } catch (const soniox::SonioxApiException& e) {
        std::cerr << "API error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
