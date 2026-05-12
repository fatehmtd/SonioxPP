# scripts/run_all_examples.ps1
# Runs all SonioxPP examples end-to-end and prints a pass/fail summary.
#
# Prerequisites:
#   - SONIOX_API_KEY must be set in the environment
#   - Build with: cmake -B build -DSONIOX_BUILD_EXAMPLES=ON && cmake --build build
#
# Usage:
#   $env:SONIOX_API_KEY = "your_key_here"
#   .\scripts\run_all_examples.ps1

$ErrorActionPreference = "Stop"

$RootDir  = Split-Path $PSScriptRoot -Parent
$BuildDir = Join-Path $RootDir "build\examples"
$DataDir  = Join-Path $RootDir "data"
$Results  = @()

if (-not $env:SONIOX_API_KEY) {
    Write-Error "SONIOX_API_KEY is not set."
    exit 1
}

New-Item -ItemType Directory -Force -Path $DataDir | Out-Null

function Run-Example([string]$Name, [scriptblock]$Action, [scriptblock]$Verify) {
    Write-Host ""
    Write-Host "──── $Name ────" -ForegroundColor Cyan
    try {
        & $Action
        & $Verify
        Write-Host "  PASS" -ForegroundColor Green
        $script:Results += [pscustomobject]@{ Name = $Name; Status = "PASS"; Error = "" }
    } catch {
        Write-Host "  FAIL: $_" -ForegroundColor Red
        $script:Results += [pscustomobject]@{ Name = $Name; Status = "FAIL"; Error = "$_" }
    }
}

function Assert-File([string]$Path, [int]$MinBytes = 10240) {
    if (-not (Test-Path $Path)) { throw "Output file not found: $Path" }
    $sz = (Get-Item $Path).Length
    if ($sz -lt $MinBytes) { throw "Output file too small ($sz bytes): $Path" }
    Write-Host "  -> $Path ($sz bytes)" -ForegroundColor DarkGray
}

# ── TTS ───────────────────────────────────────────────────────────────────────

Run-Example "TTS REST  ->  data\sample_audio.wav" {
    & "$BuildDir\tts_rest\Debug\soniox_tts_rest.exe" `
        --text "Welcome to Soniox, an advanced speech AI platform. Our library supports real-time transcription and text-to-speech synthesis." `
        --voice Adrian --format wav --out "$DataDir\sample_audio.wav"
} {
    Assert-File "$DataDir\sample_audio.wav"
}

Run-Example "TTS Realtime WebSocket  ->  data\tts_realtime_output.wav" {
    & "$BuildDir\tts_realtime\Debug\soniox_tts_realtime.exe" `
        --text "Hello from real-time TTS streaming." `
        --voice Maya --format wav --out "$DataDir\tts_realtime_output.wav"
} {
    Assert-File "$DataDir\tts_realtime_output.wav"
}

Run-Example "TTS Multiplex  (3 concurrent streams)  ->  data\multiplex_s*.wav" {
    Push-Location $DataDir
    try {
        & "$BuildDir\tts_multiplex\Debug\soniox_tts_multiplex.exe" --format wav
    } finally {
        Pop-Location
    }
} {
    Assert-File "$DataDir\multiplex_s1.wav"
    Assert-File "$DataDir\multiplex_s2.wav"
    Assert-File "$DataDir\multiplex_s3.wav"
}

Run-Example "TTS Streaming Text (TTFA benchmark)  ->  data\tts_streaming_output.wav" {
    $script:ttfaOutput = & "$BuildDir\tts_streaming_text\Debug\soniox_tts_streaming_text.exe" `
        --file "$DataDir\sample_text.txt" --voice Noah --format wav `
        --out "$DataDir\tts_streaming_output.wav" --word-delay-ms 50
    $script:ttfaOutput | Write-Host
} {
    Assert-File "$DataDir\tts_streaming_output.wav"
    if (-not ($script:ttfaOutput -match "TTFA:")) { throw "TTFA timing not found in output" }
}

# ── STT ───────────────────────────────────────────────────────────────────────

Run-Example "STT Realtime WebSocket" {
    & "$BuildDir\realtime\Debug\soniox_realtime.exe" `
        "$DataDir\sample_audio.wav" --lang en
} {
    # exit code 0 is sufficient; tokens are printed live
}

Run-Example "STT Async REST  (with speaker diarization)" {
    $script:asyncOut = & "$BuildDir\async\Debug\soniox_async.exe" `
        "$DataDir\sample_audio.wav" --lang en --diarize
    $script:asyncOut | Write-Host
} {
    if (-not ($script:asyncOut -match "Transcript")) { throw "'Transcript' not found in output" }
}

# Upload once; reuse the file ID for both table and SRT to avoid a second upload.
$script:tsFileId = ""
Run-Example "STT Async Timestamps  --  upload" {
    $script:uploadOut = & "$BuildDir\async_timestamps\Debug\soniox_async_timestamps.exe" `
        "$DataDir\sample_audio.wav" --lang en --output table --no-cleanup
    $script:uploadOut | Write-Host
    $m = [regex]::Match($script:uploadOut, "File ID:\s*(\S+)")
    if (-not $m.Success) { throw "File ID not found in upload output" }
    $script:tsFileId = $m.Groups[1].Value
    if (-not ($script:uploadOut -match "start_ms")) { throw "Expected 'start_ms' header in table output" }
    $script:uploadOut | Out-File "$DataDir\timestamps_table.txt" -Encoding utf8
} {
    if (-not $script:tsFileId) { throw "File ID is empty after upload step" }
}

Run-Example "STT Async Timestamps  --  SRT  ->  data\output.srt" {
    $script:srtOut = & "$BuildDir\async_timestamps\Debug\soniox_async_timestamps.exe" `
        --file-id $script:tsFileId --lang en --output srt
    $script:srtOut | Write-Host
    $script:srtOut | Out-File "$DataDir\output.srt" -Encoding utf8
} {
    if (-not ($script:srtOut -match "-->")) { throw "Expected SRT timestamp marker '-->' in output" }
}

Run-Example "STT Realtime Translation  en -> es" {
    $script:transOut = & "$BuildDir\realtime_translation\Debug\soniox_realtime_translation.exe" `
        "$DataDir\sample_audio.wav" --src-lang en --tgt-lang es
    $script:transOut | Write-Host
} {
    if (-not ($script:transOut -match "\[orig\]")) { throw "Expected [orig] label in output" }
    if (-not ($script:transOut -match "\[tran\]")) { throw "Expected [tran] label in output" }
}

# ── Summary ───────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "═══════════════════════ SUMMARY ═══════════════════════" -ForegroundColor White
$Results | Format-Table Name, Status, Error -AutoSize

$passed = ($Results | Where-Object Status -eq "PASS").Count
$failed = ($Results | Where-Object Status -eq "FAIL").Count
$color  = if ($failed -gt 0) { "Yellow" } else { "Green" }
Write-Host "$passed passed, $failed failed" -ForegroundColor $color

Write-Host ""
Write-Host "Manual test (microphone required):" -ForegroundColor DarkGray
Write-Host "  $BuildDir\realtime_mic\Debug\soniox_realtime_mic.exe --lang en --diarize" -ForegroundColor DarkGray
Write-Host "  (speak into the microphone, press Enter to stop)" -ForegroundColor DarkGray

if ($failed -gt 0) { exit 1 }
