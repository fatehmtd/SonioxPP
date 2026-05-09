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
    HINTERNET session{nullptr};
    HINTERNET connection{nullptr};
    HINTERNET request{nullptr};
    HINTERNET websocket{nullptr};
    std::thread receive_thread;
};

WinHttpWebSocketTransport::WinHttpWebSocketTransport() : impl_(std::make_unique<Impl>()) {}

WinHttpWebSocketTransport::~WinHttpWebSocketTransport()
{
    close();
    if (impl_->receive_thread.joinable()) {
        impl_->receive_thread.join();
    }
    if (impl_->websocket)   { WinHttpCloseHandle(impl_->websocket);   impl_->websocket   = nullptr; }
    if (impl_->request)     { WinHttpCloseHandle(impl_->request);     impl_->request     = nullptr; }
    if (impl_->connection)  { WinHttpCloseHandle(impl_->connection);  impl_->connection  = nullptr; }
    if (impl_->session)     { WinHttpCloseHandle(impl_->session);     impl_->session     = nullptr; }
}

void WinHttpWebSocketTransport::setOnOpen(OpenHandler handler)
{
    std::lock_guard<std::mutex> guard(callback_mutex_);
    on_open_ = std::move(handler);
}

void WinHttpWebSocketTransport::setOnTextMessage(TextMessageHandler handler)
{
    std::lock_guard<std::mutex> guard(callback_mutex_);
    on_text_ = std::move(handler);
}

void WinHttpWebSocketTransport::setOnBinaryMessage(BinaryMessageHandler handler)
{
    std::lock_guard<std::mutex> guard(callback_mutex_);
    on_binary_ = std::move(handler);
}

void WinHttpWebSocketTransport::setOnError(ErrorHandler handler)
{
    std::lock_guard<std::mutex> guard(callback_mutex_);
    on_error_ = std::move(handler);
}

void WinHttpWebSocketTransport::setOnClose(CloseHandler handler)
{
    std::lock_guard<std::mutex> guard(callback_mutex_);
    on_close_ = std::move(handler);
}

void WinHttpWebSocketTransport::connect(const WebSocketConnectOptions& options)
{
    const ParsedWsUrl url = parseWsUrl(options.url);

    impl_->session = WinHttpOpen(
        L"SonioxPP/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!impl_->session) {
        throw std::runtime_error("[sonioxpp] WinHttpOpen failed: " + std::to_string(GetLastError()));
    }

    impl_->connection = WinHttpConnect(impl_->session, url.host.c_str(), url.port, 0);
    if (!impl_->connection) {
        throw std::runtime_error("[sonioxpp] WinHttpConnect failed: " + std::to_string(GetLastError()));
    }

    const DWORD reqFlags = WINHTTP_FLAG_BYPASS_PROXY_CACHE | (url.secure ? WINHTTP_FLAG_SECURE : 0);
    impl_->request = WinHttpOpenRequest(
        impl_->connection, L"GET", url.path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
    if (!impl_->request) {
        throw std::runtime_error("[sonioxpp] WinHttpOpenRequest failed: " + std::to_string(GetLastError()));
    }

    if (!WinHttpSetOption(impl_->request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        throw std::runtime_error("[sonioxpp] WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET failed: " + std::to_string(GetLastError()));
    }

    for (const auto& [key, value] : options.headers) {
        const std::wstring header =
            std::wstring(key.begin(), key.end()) + L": " +
            std::wstring(value.begin(), value.end()) + L"\r\n";
        WinHttpAddRequestHeaders(impl_->request, header.c_str(),
                                  static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD);
    }

    if (!WinHttpSendRequest(impl_->request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                             WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        throw std::runtime_error("[sonioxpp] WinHttpSendRequest failed: " + std::to_string(GetLastError()));
    }

    if (!WinHttpReceiveResponse(impl_->request, nullptr)) {
        throw std::runtime_error("[sonioxpp] WinHttpReceiveResponse failed: " + std::to_string(GetLastError()));
    }

    impl_->websocket = WinHttpWebSocketCompleteUpgrade(impl_->request, 0);
    if (!impl_->websocket) {
        throw std::runtime_error("[sonioxpp] WinHttpWebSocketCompleteUpgrade failed: " + std::to_string(GetLastError()));
    }

    is_open_.store(true);

    // Background thread: receive loop with fragment reassembly
    impl_->receive_thread = std::thread([this] {
        std::vector<std::uint8_t> frame(65536);
        std::vector<std::uint8_t> message;
        bool pending_binary = false;

        while (is_open_.load()) {
            DWORD bytes_read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE buf_type{};

            const DWORD rc = WinHttpWebSocketReceive(
                impl_->websocket,
                frame.data(), static_cast<DWORD>(frame.size()),
                &bytes_read, &buf_type);

            if (rc != ERROR_SUCCESS) {
                if (is_open_.exchange(false)) {
                    ErrorHandler cb;
                    {
                        std::lock_guard<std::mutex> g(callback_mutex_);
                        cb = on_error_;
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
                    std::lock_guard<std::mutex> g(callback_mutex_);
                    cb = on_text_;
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
                    std::lock_guard<std::mutex> g(callback_mutex_);
                    cb = on_binary_;
                }
                if (cb) {
                    cb(message);
                }
                message.clear();
                pending_binary = false;
                break;
            }

            case WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE:
                is_open_.store(false);
                {
                    CloseHandler cb;
                    {
                        std::lock_guard<std::mutex> g(callback_mutex_);
                        cb = on_close_;
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
        std::lock_guard<std::mutex> guard(callback_mutex_);
        cb = on_open_;
    }
    if (cb) {
        cb();
    }
}

void WinHttpWebSocketTransport::sendText(const std::string& message)
{
    if (!is_open_.load()) {
        throw std::runtime_error("[sonioxpp] WebSocket is not open");
    }
    const DWORD rc = WinHttpWebSocketSend(
        impl_->websocket,
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
    if (!is_open_.load()) {
        throw std::runtime_error("[sonioxpp] WebSocket is not open");
    }
    const DWORD rc = WinHttpWebSocketSend(
        impl_->websocket,
        WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
        const_cast<void*>(static_cast<const void*>(payload.data())),
        static_cast<DWORD>(payload.size()));
    if (rc != ERROR_SUCCESS) {
        throw std::runtime_error("[sonioxpp] WinHttpWebSocketSend failed: " + std::to_string(rc));
    }
}

void WinHttpWebSocketTransport::close()
{
    if (!is_open_.exchange(false)) {
        return;
    }
    if (impl_->websocket) {
        WinHttpWebSocketClose(impl_->websocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
    }
}

bool WinHttpWebSocketTransport::isOpen() const
{
    return is_open_.load();
}

} // namespace soniox::transport
