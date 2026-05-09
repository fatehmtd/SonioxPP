#include <sonioxpp/transport/lws_websocket_transport.hpp>

#include <libwebsockets.h>

#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace soniox::transport {

// ---------------------------------------------------------------------------
// URL parser
// ---------------------------------------------------------------------------
namespace {

struct ParsedWsUrl {
    std::string address;
    int         port{443};
    std::string path;
    bool        tls{true};
};

ParsedWsUrl parseWsUrl(const std::string& url)
{
    ParsedWsUrl out;
    std::string rest;

    if (url.rfind("wss://", 0) == 0) {
        out.tls  = true;
        out.port = 443;
        rest     = url.substr(6);
    } else if (url.rfind("ws://", 0) == 0) {
        out.tls  = false;
        out.port = 80;
        rest     = url.substr(5);
    } else {
        throw std::runtime_error("[sonioxpp] Invalid WebSocket URL: " + url);
    }

    const auto slash    = rest.find('/');
    const auto hostPort = (slash != std::string::npos) ? rest.substr(0, slash) : rest;
    out.path            = (slash != std::string::npos) ? rest.substr(slash) : "/";

    const auto colon = hostPort.find(':');
    if (colon != std::string::npos) {
        out.address = hostPort.substr(0, colon);
        out.port    = std::stoi(hostPort.substr(colon + 1));
    } else {
        out.address = hostPort;
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct LwsWebSocketTransport::Impl {
    LwsWebSocketTransport* owner{nullptr};

    // connection parameters (written before service thread starts)
    std::string address;
    int         port{0};
    std::string path;
    bool        tls{true};
    std::map<std::string, std::string> headers;

    // LWS handles
    lws_context* ctx{nullptr};
    lws*         wsi{nullptr};

    // service thread
    std::thread       service_thread;
    std::atomic<bool> stopping{false};

    // connect() synchronisation
    std::mutex              connect_mutex;
    std::condition_variable connect_cv;
    bool        connect_done{false};
    bool        connect_failed{false};
    std::string connect_error;

    // outbound queue — each message has LWS_PRE bytes of padding prepended
    struct OutboundMsg {
        bool                       is_binary{false};
        std::vector<unsigned char> data;
    };
    std::mutex              queue_mutex;
    std::queue<OutboundMsg> send_queue;

    // graceful close flag
    std::atomic<bool> closing{false};

    // fragment reassembly
    std::vector<uint8_t> frag_buf;
    bool frag_is_binary{false};
};

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static int lwsCallback(lws* wsi, lws_callback_reasons reason,
                       void* user, void* in, size_t len);

static const lws_protocols kProtocols[] = {
    { "sonioxpp-ws", lwsCallback, 0, 65536, 0, nullptr, 0 },
    LWS_PROTOCOL_LIST_TERM
};

// ---------------------------------------------------------------------------
// LWS callback
// ---------------------------------------------------------------------------

static int lwsCallback(lws* wsi, lws_callback_reasons reason,
                       void* /*user*/, void* in, size_t len)
{
    auto* impl = static_cast<LwsWebSocketTransport::Impl*>(
                     lws_context_user(lws_get_context(wsi)));
    if (!impl) return 0;

    switch (reason) {

    // ------------------------------------------------------------------
    // Inject custom HTTP headers into the upgrade request
    // ------------------------------------------------------------------
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
        auto** p   = reinterpret_cast<unsigned char**>(in);
        auto*  end = *p + len;
        for (const auto& [key, value] : impl->headers) {
            if (lws_add_http_header_by_name(
                    wsi,
                    reinterpret_cast<const unsigned char*>(key.c_str()),
                    reinterpret_cast<const unsigned char*>(value.c_str()),
                    static_cast<int>(value.size()),
                    p, end) != 0) {
                return -1;
            }
        }
        break;
    }

    // ------------------------------------------------------------------
    // Handshake complete
    // ------------------------------------------------------------------
    case LWS_CALLBACK_CLIENT_ESTABLISHED: {
        {
            std::lock_guard<std::mutex> lk(impl->connect_mutex);
            impl->connect_done   = true;
            impl->connect_failed = false;
        }
        impl->connect_cv.notify_one();
        break;
    }

    // ------------------------------------------------------------------
    // Connection failed before the WebSocket handshake completed
    // ------------------------------------------------------------------
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
        const std::string err = (in && len > 0)
            ? std::string(static_cast<const char*>(in), len)
            : "connection error";
        {
            std::lock_guard<std::mutex> lk(impl->connect_mutex);
            impl->connect_done   = true;
            impl->connect_failed = true;
            impl->connect_error  = err;
        }
        impl->connect_cv.notify_one();
        impl->wsi = nullptr;
        break;
    }

    // ------------------------------------------------------------------
    // Incoming data (possibly fragmented)
    // ------------------------------------------------------------------
    case LWS_CALLBACK_CLIENT_RECEIVE: {
        const bool is_bin = (lws_frame_is_binary(wsi) != 0);
        const auto* data  = static_cast<const uint8_t*>(in);

        if (impl->frag_buf.empty()) {
            impl->frag_is_binary = is_bin;
        }
        impl->frag_buf.insert(impl->frag_buf.end(), data, data + len);

        if (lws_is_final_fragment(wsi)) {
            if (impl->frag_is_binary) {
                IWebSocketTransport::BinaryMessageHandler cb;
                {
                    std::lock_guard<std::mutex> g(impl->owner->callback_mutex_);
                    cb = impl->owner->on_binary_;
                }
                if (cb) cb(impl->frag_buf);
            } else {
                IWebSocketTransport::TextMessageHandler cb;
                {
                    std::lock_guard<std::mutex> g(impl->owner->callback_mutex_);
                    cb = impl->owner->on_text_;
                }
                if (cb) cb(std::string(impl->frag_buf.begin(), impl->frag_buf.end()));
            }
            impl->frag_buf.clear();
        }
        break;
    }

    // ------------------------------------------------------------------
    // Socket ready for writing
    // ------------------------------------------------------------------
    case LWS_CALLBACK_CLIENT_WRITEABLE: {
        if (impl->closing.load()) {
            lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, nullptr, 0);
            return -1;
        }

        std::lock_guard<std::mutex> lk(impl->queue_mutex);
        if (!impl->send_queue.empty()) {
            auto& msg           = impl->send_queue.front();
            const size_t paylen = msg.data.size() - LWS_PRE;
            lws_write(wsi,
                      msg.data.data() + LWS_PRE,
                      paylen,
                      msg.is_binary ? LWS_WRITE_BINARY : LWS_WRITE_TEXT);
            impl->send_queue.pop();
            if (!impl->send_queue.empty()) {
                lws_callback_on_writable(wsi);
            }
        }
        break;
    }

    // ------------------------------------------------------------------
    // Woken by lws_cancel_service() from sendText/sendBinary/close
    // ------------------------------------------------------------------
    case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
        if (impl->wsi) {
            std::lock_guard<std::mutex> lk(impl->queue_mutex);
            if (!impl->send_queue.empty() || impl->closing.load()) {
                lws_callback_on_writable(impl->wsi);
            }
        }
        break;
    }

    // ------------------------------------------------------------------
    // Connection closed (either side)
    // ------------------------------------------------------------------
    case LWS_CALLBACK_CLIENT_CLOSED:
    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE: {
        impl->wsi = nullptr;
        impl->stopping.store(true);

        if (impl->owner->is_open_.exchange(false)) {
            IWebSocketTransport::CloseHandler cb;
            {
                std::lock_guard<std::mutex> g(impl->owner->callback_mutex_);
                cb = impl->owner->on_close_;
            }
            if (cb) cb();
        }
        break;
    }

    default:
        break;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

LwsWebSocketTransport::LwsWebSocketTransport()
    : impl_(std::make_unique<Impl>())
{
    impl_->owner = this;
}

LwsWebSocketTransport::~LwsWebSocketTransport()
{
    if (is_open_.load()) {
        impl_->closing.store(true);
        if (impl_->ctx) lws_cancel_service(impl_->ctx);
    }
    impl_->stopping.store(true);
    if (impl_->ctx) lws_cancel_service(impl_->ctx);

    if (impl_->service_thread.joinable()) {
        impl_->service_thread.join();
    }
    if (impl_->ctx) {
        lws_context_destroy(impl_->ctx);
        impl_->ctx = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Handler setters
// ---------------------------------------------------------------------------

void LwsWebSocketTransport::setOnOpen(OpenHandler h)
    { std::lock_guard<std::mutex> g(callback_mutex_); on_open_   = std::move(h); }
void LwsWebSocketTransport::setOnTextMessage(TextMessageHandler h)
    { std::lock_guard<std::mutex> g(callback_mutex_); on_text_   = std::move(h); }
void LwsWebSocketTransport::setOnBinaryMessage(BinaryMessageHandler h)
    { std::lock_guard<std::mutex> g(callback_mutex_); on_binary_ = std::move(h); }
void LwsWebSocketTransport::setOnError(ErrorHandler h)
    { std::lock_guard<std::mutex> g(callback_mutex_); on_error_  = std::move(h); }
void LwsWebSocketTransport::setOnClose(CloseHandler h)
    { std::lock_guard<std::mutex> g(callback_mutex_); on_close_  = std::move(h); }
bool LwsWebSocketTransport::isOpen() const { return is_open_.load(); }

// ---------------------------------------------------------------------------
// connect()
// ---------------------------------------------------------------------------

void LwsWebSocketTransport::connect(const WebSocketConnectOptions& options)
{
    lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);

    const ParsedWsUrl parsed = parseWsUrl(options.url);
    impl_->address = parsed.address;
    impl_->port    = parsed.port;
    impl_->path    = parsed.path;
    impl_->tls     = parsed.tls;
    impl_->headers = options.headers;

    lws_context_creation_info ctx_info{};
    ctx_info.port      = CONTEXT_PORT_NO_LISTEN;
    ctx_info.protocols = kProtocols;
    ctx_info.user      = impl_.get();
    ctx_info.options   = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    impl_->ctx = lws_create_context(&ctx_info);
    if (!impl_->ctx) {
        throw std::runtime_error("[sonioxpp] lws_create_context failed");
    }

    lws_client_connect_info ccinfo{};
    ccinfo.context        = impl_->ctx;
    ccinfo.address        = impl_->address.c_str();
    ccinfo.port           = impl_->port;
    ccinfo.path           = impl_->path.c_str();
    ccinfo.host           = impl_->address.c_str();
    ccinfo.origin         = impl_->address.c_str();
    ccinfo.protocol       = kProtocols[0].name;
    ccinfo.ssl_connection = impl_->tls ? LCCSCF_USE_SSL : 0;

    impl_->wsi = lws_client_connect_via_info(&ccinfo);
    if (!impl_->wsi) {
        lws_context_destroy(impl_->ctx);
        impl_->ctx = nullptr;
        throw std::runtime_error("[sonioxpp] lws_client_connect_via_info failed");
    }

    // Start the service thread before waiting, since ESTABLISHED fires from lws_service()
    impl_->service_thread = std::thread([this] {
        while (!impl_->stopping.load()) {
            lws_service(impl_->ctx, 50);
        }
    });

    {
        std::unique_lock<std::mutex> lk(impl_->connect_mutex);
        impl_->connect_cv.wait(lk, [this] { return impl_->connect_done; });
    }

    if (impl_->connect_failed) {
        impl_->stopping.store(true);
        lws_cancel_service(impl_->ctx);
        impl_->service_thread.join();
        lws_context_destroy(impl_->ctx);
        impl_->ctx = nullptr;
        throw std::runtime_error("[sonioxpp] WebSocket connect failed: "
                                 + impl_->connect_error);
    }

    is_open_.store(true);

    OpenHandler cb;
    { std::lock_guard<std::mutex> g(callback_mutex_); cb = on_open_; }
    if (cb) cb();
}

// ---------------------------------------------------------------------------
// sendText() / sendBinary()
// ---------------------------------------------------------------------------

void LwsWebSocketTransport::sendText(const std::string& message)
{
    if (!is_open_.load()) {
        throw std::runtime_error("[sonioxpp] WebSocket is not open");
    }

    Impl::OutboundMsg msg;
    msg.is_binary = false;
    msg.data.resize(LWS_PRE + message.size());
    std::memcpy(msg.data.data() + LWS_PRE, message.data(), message.size());

    { std::lock_guard<std::mutex> lk(impl_->queue_mutex); impl_->send_queue.push(std::move(msg)); }
    lws_cancel_service(impl_->ctx);
}

void LwsWebSocketTransport::sendBinary(const std::vector<std::uint8_t>& payload)
{
    if (!is_open_.load()) {
        throw std::runtime_error("[sonioxpp] WebSocket is not open");
    }

    Impl::OutboundMsg msg;
    msg.is_binary = true;
    msg.data.resize(LWS_PRE + payload.size());
    if (!payload.empty()) {
        std::memcpy(msg.data.data() + LWS_PRE, payload.data(), payload.size());
    }

    { std::lock_guard<std::mutex> lk(impl_->queue_mutex); impl_->send_queue.push(std::move(msg)); }
    lws_cancel_service(impl_->ctx);
}

// ---------------------------------------------------------------------------
// close()
// ---------------------------------------------------------------------------

void LwsWebSocketTransport::close()
{
    if (!is_open_.exchange(false)) return;

    impl_->closing.store(true);
    lws_cancel_service(impl_->ctx);
}

} // namespace soniox::transport
