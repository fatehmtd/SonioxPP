#include <sonioxpp/realtime_client.hpp>
#include <sonioxpp/stt_client.hpp>

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace soniox {
namespace {

using json = nlohmann::json;

json buildContextJson(const Context& context)
{
    json ctx = json::object();
    if (!context.general.empty()) {
        ctx["general"] = context.general;
    }
    if (!context.text.empty()) {
        ctx["text"] = context.text;
    }
    if (!context.terms.empty()) {
        ctx["terms"] = context.terms;
    }
    if (!context.translation_terms.empty()) {
        ctx["translation_terms"] = context.translation_terms;
    }
    return ctx;
}

json buildTranslationJson(const Translation& translation)
{
    json t = json::object();
    if (!translation.type.empty()) {
        t["type"] = translation.type;
    }
    if (!translation.language_a.empty()) {
        t["language_a"] = translation.language_a;
    }
    if (!translation.language_b.empty()) {
        t["language_b"] = translation.language_b;
    }
    return t;
}

} // namespace

class RealtimeClientImpl {
public:
    RealtimeClientImpl()
        : realtime_client_()
    {
        realtime_client_.setOnMessage([this](const std::string& raw) { handleMessage(raw); });

        realtime_client_.setOnError([this](const std::string& message) {
            if (on_error_) {
                on_error_(RealtimeError{0, message});
            }
            finished_.store(true);
            done_cv_.notify_all();
        });

        realtime_client_.setOnClosed([this] {
            finished_.store(true);
            done_cv_.notify_all();
        });
    }

    void connect(const RealtimeConfig& config)
    {
        SttRealtimeConfig request;
        request.api_key = config.api_key;
        request.model = config.model;
        request.audio_format = config.audio_format;
        request.sample_rate = config.sample_rate;
        request.num_channels = config.num_channels;
        request.language_hints = config.language_hints;
        request.enable_language_identification = config.enable_language_identification;
        request.enable_speaker_diarization = config.enable_speaker_diarization;
        request.enable_endpoint_detection = config.enable_endpoint_detection;

        const json context = buildContextJson(config.context);
        if (!context.empty()) {
            request.context_json = context.dump();
        }

        const json translation = buildTranslationJson(config.translation);
        if (!translation.empty()) {
            request.translation_json = translation.dump();
        }

        finished_.store(false);
        realtime_client_.connect(request);
    }

    void sendAudio(const uint8_t* data, size_t size)
    {
        if (size == 0 || data == nullptr) {
            return;
        }

        std::vector<std::uint8_t> bytes(data, data + size);
        realtime_client_.sendAudio(bytes);
    }

    void sendEndOfAudio()
    {
        realtime_client_.sendEndOfAudio();
    }

    void run()
    {
        std::unique_lock<std::mutex> lock(done_mutex_);
        done_cv_.wait(lock, [this] { return finished_.load(); });
    }

    void close()
    {
        realtime_client_.close();
    }

    OnTokensCallback on_tokens_;
    OnFinishedCallback on_finished_;
    OnErrorCallback on_error_;

private:
    void handleMessage(const std::string& rawMessage)
    {
        json payload;
        try {
            payload = json::parse(rawMessage);
        } catch (const json::exception&) {
            return;
        }

        const std::string error_message = payload.value("error_message", std::string());
        if (!error_message.empty()) {
            const int error_code = payload.value("error_code", 0);
            if (on_error_) {
                on_error_(RealtimeError{error_code, error_message});
            }
            return;
        }

        if (payload.contains("tokens") && payload["tokens"].is_array() && on_tokens_) {
            std::vector<Token> tokens;
            bool has_final = false;

            for (const auto& item : payload["tokens"]) {
                Token token;
                token.text = item.value("text", std::string());
                token.is_final = item.value("is_final", false);
                token.speaker = item.value("speaker", 0);
                token.language = item.value("language", std::string());
                token.translation_status = item.value("translation_status", std::string());
                if (token.is_final) {
                    has_final = true;
                }
                tokens.push_back(std::move(token));
            }

            if (!tokens.empty()) {
                on_tokens_(tokens, has_final);
            }
        }

        if (payload.value("finished", false)) {
            finished_.store(true);
            done_cv_.notify_all();
            if (on_finished_) {
                on_finished_();
            }
        }
    }

    SttRealtimeClient realtime_client_;

    std::mutex done_mutex_;
    std::condition_variable done_cv_;
    std::atomic<bool> finished_{false};
};

RealtimeClient::RealtimeClient()
    : impl_(std::make_unique<RealtimeClientImpl>())
{
}

RealtimeClient::~RealtimeClient() = default;
RealtimeClient::RealtimeClient(RealtimeClient&&) noexcept = default;
RealtimeClient& RealtimeClient::operator=(RealtimeClient&&) noexcept = default;

void RealtimeClient::setOnTokens(OnTokensCallback onTokensCallback)
{
    impl_->on_tokens_ = std::move(onTokensCallback);
}

void RealtimeClient::setOnFinished(OnFinishedCallback onFinishedCallback)
{
    impl_->on_finished_ = std::move(onFinishedCallback);
}

void RealtimeClient::setOnError(OnErrorCallback onErrorCallback)
{
    impl_->on_error_ = std::move(onErrorCallback);
}

void RealtimeClient::connect(const RealtimeConfig& config)
{
    impl_->connect(config);
}

void RealtimeClient::sendAudio(const uint8_t* data, size_t size)
{
    impl_->sendAudio(data, size);
}

void RealtimeClient::sendAudio(const std::vector<uint8_t>& data)
{
    impl_->sendAudio(data.data(), data.size());
}

void RealtimeClient::sendEndOfAudio()
{
    impl_->sendEndOfAudio();
}

void RealtimeClient::run()
{
    impl_->run();
}

void RealtimeClient::close()
{
    impl_->close();
}

} // namespace soniox
