#include "realtime_client_impl.hpp"

namespace soniox {

// ---------------------------------------------------------------------------
// buildConfigJson — serialise RealtimeConfig to the JSON the server expects
// ---------------------------------------------------------------------------

json RealtimeClientImpl::buildConfigJson(const RealtimeConfig& config)
{
    json jsonContext = json::object();
    if (!config.context.general.empty())
        jsonContext["general"] = config.context.general;
    if (!config.context.text.empty())
        jsonContext["text"] = config.context.text;
    if (!config.context.terms.empty())
        jsonContext["terms"] = config.context.terms;
    if (!config.context.translation_terms.empty())
        jsonContext["translation_terms"] = config.context.translation_terms;

    json jsonTranslation = json::object();
    if (!config.translation.type.empty()) {
        jsonTranslation["type"] = config.translation.type;
        if (!config.translation.language_a.empty())
            jsonTranslation["target_language"] = config.translation.language_a;
        if (!config.translation.language_b.empty())
            jsonTranslation["source_language"] = config.translation.language_b;
    }

    json jsonCfg;
    jsonCfg["api_key"]      = config.api_key;
    jsonCfg["model"]        = config.model;
    jsonCfg["audio_format"] = config.audio_format;
    if (config.sample_rate  > 0) jsonCfg["sample_rate"]   = config.sample_rate;
    if (config.num_channels > 0) jsonCfg["num_channels"]  = config.num_channels;
    jsonCfg["language_hints"]                 = config.language_hints;
    jsonCfg["enable_language_identification"] = config.enable_language_identification;
    jsonCfg["enable_speaker_diarization"]     = config.enable_speaker_diarization;
    jsonCfg["enable_endpoint_detection"]      = config.enable_endpoint_detection;
    if (!jsonContext.empty())     jsonCfg["context"]     = jsonContext;
    if (!jsonTranslation.empty()) jsonCfg["translation"] = jsonTranslation;

    return jsonCfg;
}

// ---------------------------------------------------------------------------
// connect — set up WebSocket, register callbacks, start background thread,
//           block until the Open event (connection established) or an error.
// ---------------------------------------------------------------------------

void RealtimeClientImpl::connect(const RealtimeConfig& config)
{
    ix::initNetSystem();

    std::string url = std::string("wss://") +
                      endpoints::REALTIME_HOST +
                      endpoints::REALTIME_PATH;
    std::string configJson = buildConfigJson(config).dump();
    spdlog::debug("[sonioxpp] Sending realtime config: {}", configJson);

    _ws.setUrl(url);

    ix::SocketTLSOptions tlsOptions;
    tlsOptions.caFile = "SYSTEM"; // use platform certificate store
    _ws.setTLSOptions(tlsOptions);

    _ws.setExtraHeaders({{"User-Agent", USER_AGENT}});

    _ws.setOnMessageCallback([this, configJson](const ix::WebSocketMessagePtr& msg)
    {
        if (msg->type == ix::WebSocketMessageType::Open)
        {
            spdlog::debug("[sonioxpp] WebSocket opened, sending config");
            _ws.send(configJson); // text frame with config JSON
            {
                std::lock_guard<std::mutex> lk(_connectMtx);
                _connected = true;
            }
            _connectCv.notify_one();
        }
        else if (msg->type == ix::WebSocketMessageType::Message)
        {
            handleMessage(msg->str);
        }
        else if (msg->type == ix::WebSocketMessageType::Error)
        {
            spdlog::error("[sonioxpp] WebSocket error: {}", msg->errorInfo.reason);
            bool notifyConnect = false;
            {
                std::lock_guard<std::mutex> lk(_connectMtx);
                if (!_connected) {
                    _connectFailed = true;
                    _connectError  = msg->errorInfo.reason;
                    notifyConnect  = true;
                }
            }
            if (notifyConnect)
                _connectCv.notify_one();
            else if (_onError)
                _onError(RealtimeError{0, msg->errorInfo.reason});
        }
        else if (msg->type == ix::WebSocketMessageType::Close)
        {
            spdlog::info("[sonioxpp] WebSocket closed");
            _finished.store(true);
            _doneCv.notify_all();
        }
    });

    _ws.start();

    {
        std::unique_lock<std::mutex> lk(_connectMtx);
        _connectCv.wait(lk, [this] { return _connected || _connectFailed; });
    }

    if (_connectFailed) {
        _ws.stop();
        throw std::runtime_error(
            "[sonioxpp] WebSocket connect failed: " + _connectError);
    }

    spdlog::info("[sonioxpp] Connected to {}{}",
                 endpoints::REALTIME_HOST, endpoints::REALTIME_PATH);
}

// ---------------------------------------------------------------------------
// sendAudio — thread-safe (IXWebSocket serialises sends internally)
// ---------------------------------------------------------------------------

void RealtimeClientImpl::sendAudio(const uint8_t* data, size_t size)
{
    _ws.sendBinary(std::string(reinterpret_cast<const char*>(data), size));
}

// ---------------------------------------------------------------------------
// sendEndOfAudio — empty binary frame signals end-of-stream to the server
// ---------------------------------------------------------------------------

void RealtimeClientImpl::sendEndOfAudio()
{
    _ws.sendBinary("");
    spdlog::debug("[sonioxpp] Sent end-of-audio signal");
}

// ---------------------------------------------------------------------------
// run — blocks until the session finishes (finished:true or connection closed)
// ---------------------------------------------------------------------------

void RealtimeClientImpl::run()
{
    std::unique_lock<std::mutex> lk(_doneMtx);
    _doneCv.wait(lk, [this] { return _finished.load(); });
    _ws.stop();
}

// ---------------------------------------------------------------------------
// close — send a WebSocket close frame
// ---------------------------------------------------------------------------

void RealtimeClientImpl::close()
{
    _ws.close();
}

// ---------------------------------------------------------------------------
// handleMessage — parse incoming JSON and dispatch tokens / errors / finished
// ---------------------------------------------------------------------------

void RealtimeClientImpl::handleMessage(const std::string& rawMessage)
{
    json jsonMessage;
    try {
        jsonMessage = json::parse(rawMessage);
    }
    catch (const json::exception& ex) {
        spdlog::warn("[sonioxpp] Failed to parse server message: {}", ex.what());
        return;
    }

    // Protocol-level error from the server
    const std::string errorMsg = jsonMessage.value("error_message", "");
    if (!errorMsg.empty()) {
        int errorCode = jsonMessage.value("error_code", 0);
        spdlog::error("[sonioxpp] Server error {}: {}", errorCode, errorMsg);
        if (_onError) _onError(RealtimeError{errorCode, errorMsg});
        return;
    }

    // Dispatch token batch
    if (jsonMessage.contains("tokens") && _onTokens) {
        std::vector<Token> tokens;
        bool hasFinal = false;

        for (const auto& jsonToken : jsonMessage["tokens"]) {
            Token token;
            token.text               = jsonToken.value("text", "");
            token.is_final           = jsonToken.value("is_final", false);
            token.speaker            = jsonToken.value("speaker", 0);
            token.language           = jsonToken.value("language", "");
            token.translation_status = jsonToken.value("translation_status", "");
            if (token.is_final) hasFinal = true;
            tokens.push_back(std::move(token));
        }

        if (!tokens.empty()) _onTokens(tokens, hasFinal);
    }

    // Session completion
    if (jsonMessage.value("finished", false)) {
        _finished.store(true);
        _doneCv.notify_all();
        if (_onFinished) _onFinished();
    }
}

// ---------------------------------------------------------------------------
// RealtimeClient — public interface (pimpl forwarding)
// ---------------------------------------------------------------------------

RealtimeClient::RealtimeClient() : impl_(std::make_unique<RealtimeClientImpl>()) {}
RealtimeClient::~RealtimeClient() = default;
RealtimeClient::RealtimeClient(RealtimeClient&&)            noexcept = default;
RealtimeClient& RealtimeClient::operator=(RealtimeClient&&) noexcept = default;

void RealtimeClient::setOnTokens(OnTokensCallback cb)    { impl_->_onTokens   = std::move(cb); }
void RealtimeClient::setOnFinished(OnFinishedCallback cb) { impl_->_onFinished = std::move(cb); }
void RealtimeClient::setOnError(OnErrorCallback cb)      { impl_->_onError    = std::move(cb); }

void RealtimeClient::connect(const RealtimeConfig& config) { impl_->connect(config); }

void RealtimeClient::sendAudio(const uint8_t* data, size_t size) {
    impl_->sendAudio(data, size);
}
void RealtimeClient::sendAudio(const std::vector<uint8_t>& data) {
    impl_->sendAudio(data.data(), data.size());
}
void RealtimeClient::sendEndOfAudio() { impl_->sendEndOfAudio(); }
void RealtimeClient::run()   { impl_->run(); }
void RealtimeClient::close() { impl_->close(); }

} // namespace soniox
