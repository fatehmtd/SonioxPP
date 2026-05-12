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
        // one_way: API expects "target_language"; two_way: API expects "language_a"
        const char* key = (translation.type == stt::translation_types::one_way)
                          ? "target_language" : "language_a";
        t[key] = translation.language_a;
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
        : _realtimeClient()
    {
        _realtimeClient.setOnMessage([this](const std::string& raw) { handleMessage(raw); });

        _realtimeClient.setOnError([this](const std::string& message) {
            if (_onError) {
                _onError(RealtimeError{0, message});
            }
            _finished.store(true);
            _doneCv.notify_all();
        });

        _realtimeClient.setOnClosed([this] {
            _finished.store(true);
            _doneCv.notify_all();
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

        _finished.store(false);
        _realtimeClient.connect(request);
    }

    void sendAudio(const uint8_t* data, size_t size)
    {
        if (size == 0 || data == nullptr) {
            return;
        }

        std::vector<std::uint8_t> bytes(data, data + size);
        _realtimeClient.sendAudio(bytes);
    }

    void sendEndOfAudio()
    {
        _realtimeClient.sendEndOfAudio();
    }

    void run()
    {
        std::unique_lock<std::mutex> lock(_doneMutex);
        _doneCv.wait(lock, [this] { return _finished.load(); });
    }

    void close()
    {
        _realtimeClient.close();
    }

    OnTokensCallback _onTokens;
    OnFinishedCallback _onFinished;
    OnErrorCallback _onError;

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
            if (_onError) {
                _onError(RealtimeError{error_code, error_message});
            }
            return;
        }

        if (payload.contains("tokens") && payload["tokens"].is_array() && _onTokens) {
            std::vector<Token> tokens;
            bool has_final = false;

            for (const auto& item : payload["tokens"]) {
                Token token;
                token.text = item.value("text", std::string());
                token.is_final = item.value("is_final", false);
                token.speaker = item.value("speaker", 0);
                const auto langIt = item.find("language");
                if (langIt != item.end() && !langIt->is_null())
                    token.language = langIt->get<std::string>();
                const auto tsIt = item.find("translation_status");
                if (tsIt != item.end() && !tsIt->is_null())
                    token.translation_status = tsIt->get<std::string>();
                if (token.is_final) {
                    has_final = true;
                }
                tokens.push_back(std::move(token));
            }

            if (!tokens.empty()) {
                _onTokens(tokens, has_final);
            }
        }

        if (payload.value("finished", false)) {
            _finished.store(true);
            _doneCv.notify_all();
            if (_onFinished) {
                _onFinished();
            }
        }
    }

    SttRealtimeClient _realtimeClient;

    std::mutex _doneMutex;
    std::condition_variable _doneCv;
    std::atomic<bool> _finished{false};
};

RealtimeClient::RealtimeClient()
    : _impl(std::make_unique<RealtimeClientImpl>())
{
}

RealtimeClient::~RealtimeClient() = default;
RealtimeClient::RealtimeClient(RealtimeClient&&) noexcept = default;
RealtimeClient& RealtimeClient::operator=(RealtimeClient&&) noexcept = default;

void RealtimeClient::setOnTokens(OnTokensCallback onTokensCallback)
{
    _impl->_onTokens = std::move(onTokensCallback);
}

void RealtimeClient::setOnFinished(OnFinishedCallback onFinishedCallback)
{
    _impl->_onFinished = std::move(onFinishedCallback);
}

void RealtimeClient::setOnError(OnErrorCallback onErrorCallback)
{
    _impl->_onError = std::move(onErrorCallback);
}

void RealtimeClient::connect(const RealtimeConfig& config)
{
    _impl->connect(config);
}

void RealtimeClient::sendAudio(const uint8_t* data, size_t size)
{
    _impl->sendAudio(data, size);
}

void RealtimeClient::sendAudio(const std::vector<uint8_t>& data)
{
    _impl->sendAudio(data.data(), data.size());
}

void RealtimeClient::sendEndOfAudio()
{
    _impl->sendEndOfAudio();
}

void RealtimeClient::run()
{
    _impl->run();
}

void RealtimeClient::close()
{
    _impl->close();
}

} // namespace soniox
