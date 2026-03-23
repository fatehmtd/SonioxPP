#pragma once

#include <sonioxpp/realtime_client.hpp>

#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXNetSystem.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>

namespace soniox {

using json = nlohmann::json;

class RealtimeClientImpl {
public:
    RealtimeClientImpl() = default;

    void connect(const RealtimeConfig& config);
    void sendAudio(const uint8_t* data, size_t size);
    void sendEndOfAudio();
    void run();
    void close();

    OnTokensCallback   _onTokens;
    OnFinishedCallback _onFinished;
    OnErrorCallback    _onError;

private:
    void handleMessage(const std::string& rawMessage);
    static json buildConfigJson(const RealtimeConfig& config);

    ix::WebSocket _ws;

    std::mutex              _connectMtx;
    std::condition_variable _connectCv;
    bool        _connected{false};
    bool        _connectFailed{false};
    std::string _connectError;

    std::mutex              _doneMtx;
    std::condition_variable _doneCv;
    std::atomic<bool>       _finished{false};
};

} // namespace soniox
