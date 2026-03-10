#include <sonioxpp/realtime_client.hpp>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <mutex>
#include <stdexcept>
#include <string>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace ssl = boost::asio::ssl;
using     tcp = net::ip::tcp;
using     json = nlohmann::json;

namespace soniox {


    // ---------------------------------------------------------------------------
    // Implementation
    // ---------------------------------------------------------------------------

    class RealtimeClientImpl {
        public:
        RealtimeClientImpl()
            : _sslCtx(ssl::context::tlsv12_client)
            , _ws(_ioc, _sslCtx)
        {
            _sslCtx.set_default_verify_paths();
            _sslCtx.set_verify_mode(ssl::verify_peer);
        }

        // -----------------------------------------------------------------------
        void connect(const RealtimeConfig& config)
        {
            tcp::resolver resolver{ _ioc };
            auto const resolvedEndpoints = resolver.resolve(
                endpoints::REALTIME_HOST,
                endpoints::REALTIME_PORT);

            beast::get_lowest_layer(_ws).connect(resolvedEndpoints);

            if (!SSL_set_tlsext_host_name(_ws.next_layer().native_handle(),
                endpoints::REALTIME_HOST))
            {
                throw std::runtime_error("[sonioxpp] Failed to set SNI hostname");
            }
            _ws.next_layer().handshake(ssl::stream_base::client);

            _ws.set_option(websocket::stream_base::decorator(
                [](websocket::request_type& req) {
                    req.set(boost::beast::http::field::user_agent, soniox::USER_AGENT);
                }));
            _ws.handshake(endpoints::REALTIME_HOST, endpoints::REALTIME_PATH);

            json jsonConfig = buildConfigJson(config);
            spdlog::debug("[sonioxpp] Sending realtime config: {}", jsonConfig.dump());
            _ws.text(true);
            _ws.write(net::buffer(jsonConfig.dump()));

            spdlog::info("[sonioxpp] Connected to {}{}",
                endpoints::REALTIME_HOST, endpoints::REALTIME_PATH);
        }

        // -----------------------------------------------------------------------
        void sendAudio(const uint8_t* data, size_t size)
        {
            std::lock_guard<std::mutex> lock(_writeMtx);
            _ws.binary(true);
            _ws.write(net::buffer(data, size));
        }

        // -----------------------------------------------------------------------
        void sendEndOfAudio()
        {
            std::lock_guard<std::mutex> lock(_writeMtx);
            _ws.binary(true);
            _ws.write(net::buffer("", 0)); // empty binary frame = end-of-audio signal
            spdlog::debug("[sonioxpp] Sent end-of-audio signal");
        }

        // -----------------------------------------------------------------------
        void run()
        {
            beast::flat_buffer readBuffer;
            beast::error_code  ec;

            while (!_finished) {
                readBuffer.clear();
                _ws.read(readBuffer, ec);

                if (ec == websocket::error::closed) break;
                if (ec) {
                    if (_onError) _onError(RealtimeError{0, ec.message()});
                    break;
                }

                handleMessage(beast::buffers_to_string(readBuffer.data()));
            }
        }

        // -----------------------------------------------------------------------
        void close()
        {
            beast::error_code ec;
            _ws.close(websocket::close_code::normal, ec);
        }

        // -----------------------------------------------------------------------
        // Callbacks — set via RealtimeClient setters
        OnTokensCallback   _onTokens;
        OnFinishedCallback _onFinished;
        OnErrorCallback    _onError;

        private:
        // -----------------------------------------------------------------------
        json buildConfigJson(const RealtimeConfig& config)
        {
            // Build the context object, which is nested under the main config JSON.
            json jsonContext  = json::object();
            if(!config.context.general.empty()) {
                jsonContext["general"] = config.context.general;
            }
            if(!config.context.text.empty()) {
                jsonContext["text"] = config.context.text;
            }
            if(!config.context.terms.empty()) {
                jsonContext["terms"] = config.context.terms;
            }
            if(!config.context.translation_terms.empty()) {
                jsonContext["translation_terms"] = config.context.translation_terms;
            }

            // Translation settings are nested under a "translation" key, separate from the main context, since they affect both transcription and translation output.
            json jsonTranslation = json::object();
            if (!config.translation.type.empty()) {
                jsonTranslation["type"] = config.translation.type;
                if (!config.translation.language_a.empty()) {
                    jsonTranslation["target_language"] = config.translation.language_a;
                }
                if (!config.translation.language_b.empty()) {
                    jsonTranslation["source_language"] = config.translation.language_b;
                }
            }

            json jsonConfig;
            jsonConfig["api_key"] = config.api_key;
            jsonConfig["model"] = config.model;
            jsonConfig["audio_format"] = config.audio_format;
            if (config.sample_rate > 0) {
                jsonConfig["sample_rate"] = config.sample_rate;
            }
            if (config.num_channels > 0) {
                jsonConfig["num_channels"] = config.num_channels;
            }
            jsonConfig["language_hints"] = config.language_hints;
            jsonConfig["enable_language_identification"] = config.enable_language_identification;
            jsonConfig["enable_speaker_diarization"] = config.enable_speaker_diarization;
            jsonConfig["enable_endpoint_detection"] = config.enable_endpoint_detection;
            if(!jsonContext.empty()) {
                jsonConfig["context"] = jsonContext;
            }
            if(!jsonTranslation.empty()) {
                jsonConfig["translation"] = jsonTranslation;
            }
            return jsonConfig;
        }

        // -----------------------------------------------------------------------
        void handleMessage(const std::string& rawMessage)
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

            // Dispatch tokens to the caller
            if (jsonMessage.contains("tokens") && _onTokens) {
                std::vector<Token> tokens;
                bool hasFinal = false;

                for (const auto& jsonToken : jsonMessage["tokens"]) {
                    Token token;
                    token.text = jsonToken.value("text", "");
                    token.is_final = jsonToken.value("is_final", false);
                    token.speaker = jsonToken.value("speaker", 0);
                    token.language = jsonToken.value("language", "");
                    token.translation_status = jsonToken.value("translation_status", "");
                    if (token.is_final) hasFinal = true;
                    tokens.push_back(std::move(token));
                }

                if (!tokens.empty()) _onTokens(tokens, hasFinal);
            }

            // Session completion
            _finished = jsonMessage.value("finished", false);
            if (_finished && _onFinished) _onFinished();
        }

        // -----------------------------------------------------------------------
        net::io_context                                          _ioc;
        ssl::context                                             _sslCtx;
        websocket::stream<beast::ssl_stream<beast::tcp_stream>> _ws;
        std::mutex                                               _writeMtx;
        bool                                                     _finished{ false };
    };

    // ---------------------------------------------------------------------------
    // RealtimeClient — public interface (pimpl forwarding)
    // ---------------------------------------------------------------------------

    RealtimeClient::RealtimeClient() : impl_(std::make_unique<RealtimeClientImpl>()) {}
    RealtimeClient::~RealtimeClient() = default;
    RealtimeClient::RealtimeClient(RealtimeClient&&)            noexcept = default;
    RealtimeClient& RealtimeClient::operator=(RealtimeClient&&) noexcept = default;

    void RealtimeClient::setOnTokens(OnTokensCallback cb) { impl_->_onTokens = std::move(cb); }
    void RealtimeClient::setOnFinished(OnFinishedCallback cb) { impl_->_onFinished = std::move(cb); }
    void RealtimeClient::setOnError(OnErrorCallback cb) { impl_->_onError = std::move(cb); }

    void RealtimeClient::connect(const RealtimeConfig& config) { impl_->connect(config); }

    void RealtimeClient::sendAudio(const uint8_t* data, size_t size) {
        impl_->sendAudio(data, size);
    }
    void RealtimeClient::sendAudio(const std::vector<uint8_t>& data) {
        impl_->sendAudio(data.data(), data.size());
    }
    void RealtimeClient::sendEndOfAudio() { impl_->sendEndOfAudio(); }
    void RealtimeClient::run() { impl_->run(); }
    void RealtimeClient::close() { impl_->close(); }

} // namespace soniox
