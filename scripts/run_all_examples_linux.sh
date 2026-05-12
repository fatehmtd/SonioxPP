#!/usr/bin/env bash
# scripts/run_all_examples_linux.sh
# Runs all SonioxPP examples end-to-end and prints a pass/fail summary.
#
# Prerequisites:
#   - SONIOX_API_KEY must be set in the environment
#   - Build with: cmake -B build -DSONIOX_BUILD_EXAMPLES=ON && cmake --build build
#
# Usage:
#   export SONIOX_API_KEY="your_key_here"
#   bash scripts/run_all_examples_linux.sh

set -euo pipefail

CYAN='\033[0;36m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
GRAY='\033[0;90m'
WHITE='\033[1;37m'
NC='\033[0m'

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/examples"
DATA_DIR="$ROOT_DIR/data"

PASS_NAMES=()
FAIL_NAMES=()
FAIL_ERRORS=()

if [[ -z "${SONIOX_API_KEY:-}" ]]; then
    echo "Error: SONIOX_API_KEY is not set." >&2
    exit 1
fi

mkdir -p "$DATA_DIR"

assert_file() {
    local path="$1"
    local min_bytes="${2:-10240}"
    if [[ ! -f "$path" ]]; then
        echo "  FAIL: Output file not found: $path" >&2
        return 1
    fi
    local sz
    sz=$(stat -c%s "$path")
    if (( sz < min_bytes )); then
        echo "  FAIL: Output file too small ($sz bytes): $path" >&2
        return 1
    fi
    echo -e "  ${GRAY}-> $path ($sz bytes)${NC}"
}

# ── TTS ───────────────────────────────────────────────────────────────────────

echo ""
echo -e "${CYAN}──── TTS REST  ->  data/sample_audio.wav ────${NC}"
(
    set +e
    "$BUILD_DIR/tts_rest/soniox_tts_rest" \
        --text "Welcome to Soniox, an advanced speech AI platform. Our library supports real-time transcription and text-to-speech synthesis." \
        --voice Adrian --format wav --out "$DATA_DIR/sample_audio.wav"
    exit_code=$?
    set -e
    if [[ $exit_code -ne 0 ]]; then exit $exit_code; fi
    assert_file "$DATA_DIR/sample_audio.wav"
) && { echo -e "  ${GREEN}PASS${NC}"; PASS_NAMES+=("TTS REST  ->  data/sample_audio.wav"); } \
  || { echo -e "  ${RED}FAIL${NC}"; FAIL_NAMES+=("TTS REST  ->  data/sample_audio.wav"); FAIL_ERRORS+=("see above"); }

echo ""
echo -e "${CYAN}──── TTS Realtime WebSocket  ->  data/tts_realtime_output.wav ────${NC}"
(
    set +e
    "$BUILD_DIR/tts_realtime/soniox_tts_realtime" \
        --text "Hello from real-time TTS streaming." \
        --voice Maya --format wav --out "$DATA_DIR/tts_realtime_output.wav"
    exit_code=$?
    set -e
    if [[ $exit_code -ne 0 ]]; then exit $exit_code; fi
    assert_file "$DATA_DIR/tts_realtime_output.wav"
) && { echo -e "  ${GREEN}PASS${NC}"; PASS_NAMES+=("TTS Realtime WebSocket  ->  data/tts_realtime_output.wav"); } \
  || { echo -e "  ${RED}FAIL${NC}"; FAIL_NAMES+=("TTS Realtime WebSocket  ->  data/tts_realtime_output.wav"); FAIL_ERRORS+=("see above"); }

echo ""
echo -e "${CYAN}──── TTS Multiplex  (3 concurrent streams)  ->  data/multiplex_s*.wav ────${NC}"
(
    set +e
    pushd "$DATA_DIR" > /dev/null
    "$BUILD_DIR/tts_multiplex/soniox_tts_multiplex" --format wav
    exit_code=$?
    popd > /dev/null
    set -e
    if [[ $exit_code -ne 0 ]]; then exit $exit_code; fi
    assert_file "$DATA_DIR/multiplex_s1.wav"
    assert_file "$DATA_DIR/multiplex_s2.wav"
    assert_file "$DATA_DIR/multiplex_s3.wav"
) && { echo -e "  ${GREEN}PASS${NC}"; PASS_NAMES+=("TTS Multiplex  (3 concurrent streams)"); } \
  || { echo -e "  ${RED}FAIL${NC}"; FAIL_NAMES+=("TTS Multiplex  (3 concurrent streams)"); FAIL_ERRORS+=("see above"); }

echo ""
echo -e "${CYAN}──── TTS Streaming Text (TTFA benchmark)  ->  data/tts_streaming_output.wav ────${NC}"
(
    set +e
    ttfa_output=$("$BUILD_DIR/tts_streaming_text/soniox_tts_streaming_text" \
        --file "$DATA_DIR/sample_text.txt" --voice Noah --format wav \
        --out "$DATA_DIR/tts_streaming_output.wav" --word-delay-ms 50 2>&1)
    exit_code=$?
    set -e
    echo "$ttfa_output"
    if [[ $exit_code -ne 0 ]]; then exit $exit_code; fi
    assert_file "$DATA_DIR/tts_streaming_output.wav"
    if ! echo "$ttfa_output" | grep -q "TTFA:"; then
        echo "  FAIL: TTFA timing not found in output" >&2
        exit 1
    fi
) && { echo -e "  ${GREEN}PASS${NC}"; PASS_NAMES+=("TTS Streaming Text (TTFA benchmark)"); } \
  || { echo -e "  ${RED}FAIL${NC}"; FAIL_NAMES+=("TTS Streaming Text (TTFA benchmark)"); FAIL_ERRORS+=("see above"); }

# ── STT ───────────────────────────────────────────────────────────────────────

echo ""
echo -e "${CYAN}──── STT Realtime WebSocket ────${NC}"
(
    "$BUILD_DIR/realtime/soniox_realtime" "$DATA_DIR/sample_audio.wav" --lang en
) && { echo -e "  ${GREEN}PASS${NC}"; PASS_NAMES+=("STT Realtime WebSocket"); } \
  || { echo -e "  ${RED}FAIL${NC}"; FAIL_NAMES+=("STT Realtime WebSocket"); FAIL_ERRORS+=("see above"); }

echo ""
echo -e "${CYAN}──── STT Async REST  (with speaker diarization) ────${NC}"
(
    set +e
    async_out=$("$BUILD_DIR/async/soniox_async" \
        "$DATA_DIR/sample_audio.wav" --lang en --diarize 2>&1)
    exit_code=$?
    set -e
    echo "$async_out"
    if [[ $exit_code -ne 0 ]]; then exit $exit_code; fi
    if ! echo "$async_out" | grep -q "Transcript"; then
        echo "  FAIL: 'Transcript' not found in output" >&2
        exit 1
    fi
) && { echo -e "  ${GREEN}PASS${NC}"; PASS_NAMES+=("STT Async REST  (with speaker diarization)"); } \
  || { echo -e "  ${RED}FAIL${NC}"; FAIL_NAMES+=("STT Async REST  (with speaker diarization)"); FAIL_ERRORS+=("see above"); }

# Upload once; reuse the file ID for both table and SRT to avoid a second upload.
TS_FILE_TMP=$(mktemp)
TS_FILE_ID=""

echo ""
echo -e "${CYAN}──── STT Async Timestamps  --  table  ->  data/timestamps_table.txt ────${NC}"
(
    set +e
    table_out=$("$BUILD_DIR/async_timestamps/soniox_async_timestamps" \
        "$DATA_DIR/sample_audio.wav" --lang en --output table --no-cleanup 2>&1)
    exit_code=$?
    set -e
    echo "$table_out"
    if [[ $exit_code -ne 0 ]]; then exit $exit_code; fi
    echo "$table_out" > "$DATA_DIR/timestamps_table.txt"
    if ! echo "$table_out" | grep -q "start_ms"; then
        echo "  FAIL: Expected 'start_ms' header in table output" >&2
        exit 1
    fi
    fid=$(echo "$table_out" | awk '/File ID:/{print $NF; exit}')
    if [[ -z "$fid" ]]; then
        echo "  FAIL: File ID not found in output" >&2
        exit 1
    fi
    echo "$fid" > "$TS_FILE_TMP"
) && {
    TS_FILE_ID=$(cat "$TS_FILE_TMP" 2>/dev/null || true)
    echo -e "  ${GREEN}PASS${NC}"
    PASS_NAMES+=("STT Async Timestamps  --  table")
} || {
    echo -e "  ${RED}FAIL${NC}"
    FAIL_NAMES+=("STT Async Timestamps  --  table")
    FAIL_ERRORS+=("see above")
}

echo ""
echo -e "${CYAN}──── STT Async Timestamps  --  SRT  ->  data/output.srt ────${NC}"
(
    set +e
    if [[ -z "$TS_FILE_ID" ]]; then
        echo "  Skipped — no file ID from upload step" >&2
        exit 1
    fi
    srt_out=$("$BUILD_DIR/async_timestamps/soniox_async_timestamps" \
        --file-id "$TS_FILE_ID" --lang en --output srt 2>&1)
    exit_code=$?
    set -e
    echo "$srt_out"
    if [[ $exit_code -ne 0 ]]; then exit $exit_code; fi
    echo "$srt_out" > "$DATA_DIR/output.srt"
    if ! echo "$srt_out" | grep -qF "-->"; then
        echo "  FAIL: Expected SRT timestamp marker '-->' in output" >&2
        exit 1
    fi
) && { echo -e "  ${GREEN}PASS${NC}"; PASS_NAMES+=("STT Async Timestamps  --  SRT"); } \
  || { echo -e "  ${RED}FAIL${NC}"; FAIL_NAMES+=("STT Async Timestamps  --  SRT"); FAIL_ERRORS+=("see above"); }

rm -f "$TS_FILE_TMP"

echo ""
echo -e "${CYAN}──── STT Realtime Translation  en -> es ────${NC}"
(
    set +e
    trans_out=$("$BUILD_DIR/realtime_translation/soniox_realtime_translation" \
        "$DATA_DIR/sample_audio.wav" --src-lang en --tgt-lang es 2>&1)
    exit_code=$?
    set -e
    echo "$trans_out"
    if [[ $exit_code -ne 0 ]]; then exit $exit_code; fi
    if ! echo "$trans_out" | grep -q "\[orig\]"; then
        echo "  FAIL: Expected [orig] label in output" >&2
        exit 1
    fi
    if ! echo "$trans_out" | grep -q "\[tran\]"; then
        echo "  FAIL: Expected [tran] label in output" >&2
        exit 1
    fi
) && { echo -e "  ${GREEN}PASS${NC}"; PASS_NAMES+=("STT Realtime Translation  en -> es"); } \
  || { echo -e "  ${RED}FAIL${NC}"; FAIL_NAMES+=("STT Realtime Translation  en -> es"); FAIL_ERRORS+=("see above"); }

# ── Summary ───────────────────────────────────────────────────────────────────

passed=${#PASS_NAMES[@]}
failed=${#FAIL_NAMES[@]}

echo ""
echo -e "${WHITE}═══════════════════════ SUMMARY ═══════════════════════${NC}"
echo ""
printf "%-60s %s\n" "Name" "Status"
printf "%-60s %s\n" "----" "------"
for name in "${PASS_NAMES[@]}"; do
    printf "%-60s ${GREEN}%s${NC}\n" "$name" "PASS"
done
for i in "${!FAIL_NAMES[@]}"; do
    printf "%-60s ${RED}%s${NC}  %s\n" "${FAIL_NAMES[$i]}" "FAIL" "${FAIL_ERRORS[$i]}"
done
echo ""

if (( failed > 0 )); then
    echo -e "${YELLOW}$passed passed, $failed failed${NC}"
else
    echo -e "${GREEN}$passed passed, $failed failed${NC}"
fi

echo ""
echo -e "${GRAY}Manual test (microphone required):${NC}"
echo -e "${GRAY}  $BUILD_DIR/realtime_mic/soniox_realtime_mic --lang en --diarize${NC}"
echo -e "${GRAY}  (speak into the microphone, press Ctrl+C to stop)${NC}"

if (( failed > 0 )); then exit 1; fi
