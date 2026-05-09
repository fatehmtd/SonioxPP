#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#elif _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <sonioxpp/transport/winhttp_websocket_transport.hpp>

#include <windows.h>
#include <winhttp.h>

#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace soniox::transport {
namespace {

struct ParsedWsUrl {
    bool secure{true};
    std::wstring host;
    INTERNET_PORT port{0};
    std::wstring path;
};

ParsedWsUrl parseWsUrl(const std::string& url)
{
    ParsedWsUrl out;

    std::string rest;
    if (url.size() > 6 && url.substr(0, 6) == "wss://") {
        out.secure = true;
        rest = url.substr(6);
    } else if (url.size() > 5 && url.substr(0, 5) == "ws://") {
        out.secure = false;
        rest = url.substr(5);
    } else {
        throw std::runtime_error("[sonioxpp] Invalid WebSocket URL: " + url);
    }

    const auto slashPos = rest.find('/');
    const std::string hostPort = (slashPos != std::string::npos) ? rest.substr(0, slashPos) : rest;
    const std::string path     = (slashPos != std::string::npos) ? rest.substr(slashPos) : "/";

    const auto colonPos = hostPort.find(':');
    if (colonPos != std::string::npos) {
        out.port = static_cast<INTERNET_PORT>(std::stoi(hostPort.substr(colonPos + 1)));
        const auto host = hostPort.substr(0, colonPos);
        out.host = std::wstring(host.begin(), host.end());
    } else {
        out.port = out.secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
        out.host = std::wstring(hostPort.begin(), hostPort.end());
    }

    out.path = std::wstring(path.begin(), path.end());
    return out;
}

} // namespace

struct WinHttpWebSocketTransport::Impl {
    HINTERNET _session{nullptr};
    HINTERNET _connection{nullptr};
    HINTERNET _request{nullptr};
    HINTERNET _websocket{nullptr};
    std::thread _receiveThread;
};

WinHttpWebSocketTransport::WinHttpWebSocketTransport() : _impl(std::make_unique<Impl>()) {}

WinHttpWebSocketTransport::~WinHttpWebSocketTransport()
{
    close();
    if (_impl->_receiveThread.joinable()) {
        _impl->_receiveThread.join();
    }
    if (_impl->_websocket)   { WinHttpCloseHandle(_impl->_websocket);   _impl->_websocket   = nullptr; }
    if (_impl->_request)     { WinHttpCloseHandle(_impl->_request);     _impl->_request     = nullptr; }
    if (_impl->_connection)  { WinHttpCloseHandle(_impl->_connection);  _impl->_connection  = nullptr; }
    if (_impl->_session)     { WinHttpCloseHandle(_impl->_session);     _impl->_session     = nullptr; }
}

void WinHttpWebSocketTransport::setOnOpen(OpenHandler handler)
{
    std::lock_guard<std::mutex> guard(_callbackMutex);
    _onOpen = std::move(handler);
}

void WinHttpWebSocketTransport::setOnTextMessage(TextMessageHandler handler)
{
    std::lock_guard<std::mutex> guard(_callbackMutex);
    _onText = std::move(handler);
}

void WinHttpWebSocketTransport::setOnBinaryMessage(BinaryMessageHandler handler)
{
    std::lock_guard<std::mutex> guard(_callbackMutex);
    _onBinary = std::move(handler);
}

void WinHttpWebSocketTransport::setOnError(ErrorHandler handler)
{
    std::lock_guard<std::mutex> guard(_callbackMutex);
    _onError = std::move(handler);
}

void WinHttpWebSocketTransport::setOnClose(CloseHandler handler)
{
    std::lock_guard<std::mutex> guard(_callbackMutex);
    _onClose = std::move(handler);
}

void WinHttpWebSocketTransport::connect(const WebSocketConnectOptions& options)
{
    const ParsedWsUrl url = parseWsUrl(options.url);

    _impl->_session = WinHttpOpen(
        L"SonioxPP/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!_impl->_session) {
        throw std::runtime_error("[sonioxpp] WinHttpOpen failed: " + std::to_string(GetLastError()));
    }

    _impl->_connection = WinHttpConnect(_impl->_session, url.host.c_str(), url.port, 0);
    if (!_impl->_connection) {
        throw std::runtime_error("[sonioxpp] WinHttpConnect failed: " + std::to_string(GetLastError()));
    }

    const DWORD reqFlags = WINHTTP_FLAG_BYPASS_PROXY_CACHE | (url.secure ? WINHTTP_FLAG_SECURE : 0);
    _impl->_request = WinHttpOpenRequest(
        _impl->_connection, L"GET", url.path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
    if (!_impl->_request) {
        throw std::runtime_error("[sonioxpp] WinHttpOpenRequest failed: " + std::to_string(GetLastError()));
    }

    if (!WinHttpSetOption(_impl->_request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        throw std::runtime_error("[sonioxpp] WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET failed: " + std::to_string(GetLastError()));
    }

    for (const auto& [key, value] : options.headers) {
        const std::wstring header =
            std::wstring(key.begin(), key.end()) + L": " +
            std::wstring(value.begin(), value.end()) + L"\r\n";
        WinHttpAddRequestHeaders(_impl->_request, header.c_str(),
                                  static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD);
    }

    if (!WinHttpSendRequest(_impl->_request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                             WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        throw std::runtime_error("[sonioxpp] WinHttpSendRequest failed: " + std::to_string(GetLastError()));
    }

    if (!WinHttpReceiveResponse(_impl->_request, nullptr)) {
        throw std::runtime_error("[sonioxpp] WinHttpReceiveResponse failed: " + std::to_string(GetLastError()));
    }

    _impl->_websocket = WinHttpWebSocketCompleteUpgrade(_impl->_request, 0);
    if (!_impl->_websocket) {
        throw std::runtime_error("[sonioxpp] WinHttpWebSocketCompleteUpgrade failed: " + std::to_string(GetLastError()));
    }

    _isOpen.store(true);

    // Background thread: receive loop with fragment reassembly
    _impl->_receiveThread = std::thread([this] {
        std::vector<std::uint8_t> frame(65536);
        std::vector<std::uint8_t> message;
        bool pending_binary = false;

        while (_isOpen.load()) {
            DWORD bytes_read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE buf_type{};

            const DWORD rc = WinHttpWebSocketReceive(
                _impl->_websocket,
                frame.data(), static_cast<DWORD>(frame.size()),
                &bytes_read, &buf_type);

            if (rc != ERROR_SUCCESS) {
                if (_isOpen.exchange(false)) {
                    ErrorHandler cb;
                    {
                        std::lock_guard<std::mutex> g(_callbackMutex);
                        cb = _onError;
                    }
                    if (cb) {
                        cb("[sonioxpp] WebSocket receive error: " + std::to_string(rc));
                    }
                }
                return;
            }

            const auto* begin = frame.data();
            const auto* end   = frame.data() + bytes_read;

            switch (buf_type) {
            case WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE:
                pending_binary = false;
                message.insert(message.end(), begin, end);
                break;

            case WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE:
                pending_binary = true;
                message.insert(message.end(), begin, end);
                break;

            case WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE: {
                message.insert(message.end(), begin, end);
                TextMessageHandler cb;
                {
                    std::lock_guard<std::mutex> g(_callbackMutex);
                    cb = _onText;
                }
                if (cb) {
                    cb(std::string(message.begin(), message.end()));
                }
                message.clear();
                pending_binary = false;
                break;
            }

            case WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE: {
                message.insert(message.end(), begin, end);
                BinaryMessageHandler cb;
                {
                    std::lock_guard<std::mutex> g(_callbackMutex);
                    cb = _onBinary;
                }
                if (cb) {
                    cb(message);
                }
                message.clear();
                pending_binary = false;
                break;
            }

            case WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE:
                _isOpen.store(false);
                {
                    CloseHandler cb;
                    {
                        std::lock_guard<std::mutex> g(_callbackMutex);
                        cb = _onClose;
                    }
                    if (cb) {
                        cb();
                    }
                }
                return;

            default:
                break;
            }
        }
    });

    OpenHandler cb;
    {
        std::lock_guard<std::mutex> guard(_callbackMutex);
        cb = _onOpen;
    }
    if (cb) {
        cb();
    }
}

void WinHttpWebSocketTransport::sendText(const std::string& message)
{
    if (!_isOpen.load()) {
        throw std::runtime_error("[sonioxpp] WebSocket is not open");
    }
    const DWORD rc = WinHttpWebSocketSend(
        _impl->_websocket,
        WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        // WinHttpWebSocketSend takes non-const void* but does not modify the buffer
        const_cast<void*>(static_cast<const void*>(message.data())),
        static_cast<DWORD>(message.size()));
    if (rc != ERROR_SUCCESS) {
        throw std::runtime_error("[sonioxpp] WinHttpWebSocketSend failed: " + std::to_string(rc));
    }
}

void WinHttpWebSocketTransport::sendBinary(const std::vector<std::uint8_t>& payload)
{
    if (!_isOpen.load()) {
        throw std::runtime_error("[sonioxpp] WebSocket is not open");
    }
    const DWORD rc = WinHttpWebSocketSend(
        _impl->_websocket,
        WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
        const_cast<void*>(static_cast<const void*>(payload.data())),
        static_cast<DWORD>(payload.size()));
    if (rc != ERROR_SUCCESS) {
        throw std::runtime_error("[sonioxpp] WinHttpWebSocketSend failed: " + std::to_string(rc));
    }
}

void WinHttpWebSocketTransport::close()
{
    if (!_isOpen.exchange(false)) {
        return;
    }
    if (_impl->_websocket) {
        WinHttpWebSocketClose(_impl->_websocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
    }
}

bool WinHttpWebSocketTransport::isOpen() const
{
    return _isOpen.load();
}

} // namespace soniox::transport
