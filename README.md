# SonioxPP

A modern C++17 library for the [Soniox](https://soniox.com) Speech-to-Text API,
supporting both **real-time WebSocket streaming** and **async REST transcription**.

---

## Features

| Feature | Real-time | Async |
|---|---|---|
| WebSocket streaming | ✓ | |
| REST API | | ✓ |
| Speaker diarization | ✓ | ✓ |
| Language identification | ✓ | ✓ |
| Endpoint detection | ✓ | |
| Translation | ✓ | ✓ |
| Custom vocabulary/context | ✓ | ✓ |
| File upload | | ✓ |

---

## Requirements

| Dependency | Version | Notes |
|---|---|---|
| C++ compiler | C++17 | GCC 9+, Clang 10+, MSVC 2019+ |
| CMake | 3.16+ | |
| Boost | 1.74+ | beast, asio, system, thread — via vcpkg or system package manager |
| OpenSSL | 1.1+ | TLS for wss:// and https:// — via vcpkg or system package manager |
| nlohmann/json | 3.12 | fetched automatically via CMake FetchContent |
| spdlog | 1.15 | fetched automatically via CMake FetchContent |

### Option A — vcpkg (recommended)

A `vcpkg.json` manifest is provided at `lib/sonioxpp/vcpkg.json`.
Point CMake at the vcpkg toolchain and it will install Boost and OpenSSL automatically:

```bash
cmake -B build \
    -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DSONIOX_BUILD_EXAMPLES=ON
cmake --build build
```

### Option B — Homebrew (macOS)

```bash
brew install boost openssl
```

### Option C — APT (Ubuntu/Debian)

```bash
sudo apt install libboost-system-dev libboost-thread-dev libssl-dev
```

---

## Build

```bash
git clone https://github.com/fatehmtd/SonioxPP.git
cd SonioxPP

# With vcpkg
cmake -B build \
    -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DSONIOX_BUILD_EXAMPLES=ON
cmake --build build

# Without vcpkg (Boost and OpenSSL must be installed via system package manager)
cmake -B build -DSONIOX_BUILD_EXAMPLES=ON
cmake --build build
```

To skip the examples:

```bash
cmake -B build -DSONIOX_BUILD_EXAMPLES=OFF
cmake --build build
```

---

## Integrating into your project

### FetchContent (recommended)

```cmake
include(FetchContent)
FetchContent_Declare(
    sonioxpp
    GIT_REPOSITORY https://github.com/fatehmtd/SonioxPP.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(sonioxpp)

target_link_libraries(your_target PRIVATE sonioxpp)
```

### Git submodule

```bash
git submodule add https://github.com/fatehmtd/SonioxPP.git third_party/SonioxPP
```

```cmake
add_subdirectory(third_party/SonioxPP/lib/sonioxpp)
target_link_libraries(your_target PRIVATE sonioxpp)
```

---

## Quick start

Set your API key (get one at [console.soniox.com](https://console.soniox.com)):

```bash
export SONIOX_API_KEY=your_key_here
```

### Real-time streaming

```cpp
#include <soniox/soniox.hpp>
#include <fstream>
#include <thread>

int main() {
    soniox::RealtimeConfig cfg;
    cfg.api_key       = std::getenv("SONIOX_API_KEY");
    cfg.language_hints = {"en"};

    soniox::RealtimeClient client;

    client.setOnTokens([](const std::vector<soniox::Token>& tokens, bool final) {
        for (auto& t : tokens) std::cout << t.text;
        std::flush(std::cout);
    });
    client.setOnFinished([] { std::cout << "\nDone.\n"; });
    client.setOnError([](int code, const std::string& msg) {
        std::cerr << "Error " << code << ": " << msg << "\n";
    });

    client.connect(cfg);

    std::thread sender([&] {
        // replace with real microphone/PCM data
        std::ifstream f("audio.wav", std::ios::binary);
        std::vector<uint8_t> buf(4096);
        while (f.read(reinterpret_cast<char*>(buf.data()), buf.size()) || f.gcount())
            client.sendAudio(buf.data(), f.gcount());
        client.sendEndOfAudio();
    });

    client.run(); // blocks until the session finishes
    sender.join();
}
```

### Async transcription (single call)

```cpp
#include <soniox/soniox.hpp>
#include <iostream>

int main() {
    soniox::AsyncConfig cfg;
    cfg.api_key                    = std::getenv("SONIOX_API_KEY");
    cfg.language_hints             = {"en"};
    cfg.enable_speaker_diarization = true;

    soniox::AsyncClient client(cfg.api_key);
    auto tokens = client.transcribeFile("audio.wav", cfg);

    for (auto& tok : tokens) std::cout << tok.text;
    std::cout << '\n';
}
```

---

## API reference

### `soniox::RealtimeConfig`

| Field | Type | Default | Description |
|---|---|---|---|
| `api_key` | `string` | — | **Required.** Soniox API key |
| `model` | `string` | `stt-rt-v4` | Transcription model |
| `audio_format` | `string` | `auto` | `auto`, `pcm_s16le`, `mp3`, … |
| `language_hints` | `vector<string>` | `{}` | BCP-47 codes, e.g. `{"en","es"}` |
| `enable_language_identification` | `bool` | `false` | Per-token language tag |
| `enable_speaker_diarization` | `bool` | `false` | Speaker IDs on tokens |
| `enable_endpoint_detection` | `bool` | `false` | Detect speech pauses |
| `context` | `Context` | | Domain hints and custom vocabulary |
| `translation` | `Translation` | | Translation settings |

### `soniox::RealtimeClient`

| Method | Description |
|---|---|
| `setOnTokens(cb)` | Register token callback `(tokens, has_final)` |
| `setOnFinished(cb)` | Called when server sends `finished: true` |
| `setOnError(cb)` | Called on network/protocol error |
| `connect(config)` | Connect and send initial config |
| `sendAudio(data, size)` | Send raw audio bytes |
| `sendAudio(vector)` | Convenience overload |
| `sendEndOfAudio()` | Signal end-of-audio to the server |
| `run()` | Block until session ends (call from receiver thread) |
| `close()` | Force-close the WebSocket |

### `soniox::AsyncConfig`

| Field | Type | Default | Description |
|---|---|---|---|
| `api_key` | `string` | — | **Required.** Soniox API key |
| `model` | `string` | `stt-async-v4` | Transcription model |
| `language_hints` | `vector<string>` | `{}` | BCP-47 codes |
| `enable_language_identification` | `bool` | `false` | Per-token language tag |
| `enable_speaker_diarization` | `bool` | `false` | Speaker IDs on tokens |
| `context` | `Context` | | Domain hints and custom vocabulary |
| `translation` | `Translation` | | Translation settings |
| `audio_url` | `string` | | Public audio URL (alternative to upload) |
| `file_id` | `string` | | Set automatically by `transcribeFile()` |

### `soniox::AsyncClient`

| Method | Description |
|---|---|
| `uploadFile(path)` | Upload local audio; returns file ID |
| `deleteFile(id)` | Delete an uploaded file |
| `createTranscription(cfg)` | Start a job; returns job ID |
| `getTranscription(id)` | Poll job status |
| `getTranscript(id)` | Retrieve completed tokens |
| `deleteTranscription(id)` | Delete a finished job |
| `transcribeFile(path, cfg)` | Full lifecycle in one call |

### `soniox::Token`

| Field | Type | Description |
|---|---|---|
| `text` | `string` | Token text |
| `is_final` | `bool` | Will not be revised |
| `speaker` | `int` | Speaker ID (diarization) |
| `language` | `string` | BCP-47 language code (lang-ID) |
| `translation_status` | `string` | e.g. `"translated"` |
| `start_ms` | `int` | Start time in ms (async only) |
| `duration_ms` | `int` | Duration in ms (async only) |

---

## Running the examples

### Real-time

```bash
export SONIOX_API_KEY=your_key
./build/examples/realtime/soniox_realtime audio.wav
./build/examples/realtime/soniox_realtime audio.wav --lang en,es --diarize
./build/examples/realtime/soniox_realtime audio.wav --chunk-ms 80 --debug
```

### Async

```bash
export SONIOX_API_KEY=your_key
# Local file (upload + transcribe + cleanup)
./build/examples/async/soniox_async audio.wav --diarize

# Public URL
./build/examples/async/soniox_async --url https://example.com/audio.wav
```

---

## License

Apache 2.0 — see [LICENSE](LICENSE).
