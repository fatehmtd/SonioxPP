#pragma once

/// @file soniox.hpp
/// @brief Convenience aggregator — include this single header to use all soniox APIs.
///
/// @code
/// #include <sonioxpp/soniox.hpp>
/// @endcode

#include "types.hpp"
#include "api_constants.hpp"
#include "realtime_client.hpp"
#include "async_client.hpp"
#include "stt_client.hpp"
#include "tts_client.hpp"
#include "auth_client.hpp"
#include "transport/http_transport.hpp"
#include "transport/websocket_transport.hpp"
#include "transport/curl_http_transport.hpp"
#ifdef _WIN32
#include "transport/winhttp_websocket_transport.hpp"
#else
#include "transport/lws_websocket_transport.hpp"
#endif
