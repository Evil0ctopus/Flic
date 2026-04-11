#include "audio_output.h"

#include "../diagnostics/sd_diagnostics.h"
#include "../subsystems/sd_manager.h"

#include <M5Unified.h>
#include <SD.h>

#include <cmath>
#include <cstring>

namespace {
constexpr size_t kWavChunkBytes = 8192;
constexpr size_t kWavBufferCount = 6;
constexpr float kWavHeadroomGain = 0.58f;
constexpr float kLimiterThreshold = 0.82f;
constexpr size_t kEdgeFadeSamples = 96;
uint8_t gWavBuffers[kWavBufferCount][kWavChunkBytes] = {};

struct WavFormatInfo {
    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    uint32_t dataSize = 0;
    uint32_t dataOffset = 0;
};

uint16_t readLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t readLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

float softLimit(float x) {
    if (x > 1.0f) {
        x = 1.0f;
    } else if (x < -1.0f) {
        x = -1.0f;
    }

    const float ax = fabsf(x);
    if (ax <= kLimiterThreshold) {
        return x;
    }

    const float sign = x < 0.0f ? -1.0f : 1.0f;
    const float over = (ax - kLimiterThreshold) / (1.0f - kLimiterThreshold);
    const float compressed = kLimiterThreshold + (1.0f - kLimiterThreshold) * (over / (1.0f + over));
    return sign * compressed;
}

void applyHeadroomAndLimiter(int16_t* samples, size_t sampleCount, bool fadeIn, bool fadeOut) {
    const size_t fadeLen = sampleCount < kEdgeFadeSamples ? sampleCount : kEdgeFadeSamples;
    for (size_t i = 0; i < sampleCount; ++i) {
        float x = static_cast<float>(samples[i]) / 32768.0f;
        x *= kWavHeadroomGain;

        if (fadeIn && i < fadeLen) {
            x *= static_cast<float>(i) / static_cast<float>(fadeLen);
        }
        if (fadeOut && i >= (sampleCount - fadeLen)) {
            const size_t tailIndex = i - (sampleCount - fadeLen);
            x *= 1.0f - (static_cast<float>(tailIndex) / static_cast<float>(fadeLen));
        }

        x = softLimit(x);

        const int32_t out = static_cast<int32_t>(x * 32767.0f);
        samples[i] = static_cast<int16_t>(out);
    }
}

bool parseWavHeader(File& file, WavFormatInfo& out) {
    uint8_t riff[12];
    if (file.read(riff, sizeof(riff)) != static_cast<int>(sizeof(riff))) {
        return false;
    }
    if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        return false;
    }

    bool foundFmt = false;
    bool foundData = false;

    while (file.available() > 0) {
        uint8_t chunkHeader[8];
        if (file.read(chunkHeader, sizeof(chunkHeader)) != static_cast<int>(sizeof(chunkHeader))) {
            break;
        }

        const uint32_t chunkSize = readLe32(chunkHeader + 4);
        const uint32_t chunkStart = file.position();

        if (memcmp(chunkHeader, "fmt ", 4) == 0 && chunkSize >= 16) {
            uint8_t fmt[16];
            if (file.read(fmt, sizeof(fmt)) != static_cast<int>(sizeof(fmt))) {
                return false;
            }
            out.audioFormat = readLe16(fmt + 0);
            out.channels = readLe16(fmt + 2);
            out.sampleRate = readLe32(fmt + 4);
            out.bitsPerSample = readLe16(fmt + 14);
            foundFmt = true;
        } else if (memcmp(chunkHeader, "data", 4) == 0) {
            out.dataSize = chunkSize;
            out.dataOffset = file.position();
            foundData = true;
            break;
        }

        const uint32_t nextChunk = chunkStart + chunkSize + (chunkSize & 1U);
        file.seek(nextChunk);
    }

    if (!foundFmt || !foundData) {
        return false;
    }
    if (out.audioFormat != 1 || out.channels != 1 || out.bitsPerSample != 16) {
        return false;
    }
    if (!(out.sampleRate == 16000 || out.sampleRate == 22050 || out.sampleRate == 24000 || out.sampleRate == 44100)) {
        return false;
    }
    return true;
}
}

namespace Flic {

namespace {
uint8_t clampVolume(uint8_t volume) {
    return volume;
}

String sanitizeSpeechText(const String& input) {
    String cleaned;
    cleaned.reserve(input.length());
    for (size_t index = 0; index < static_cast<size_t>(input.length()); ++index) {
        char c = input.charAt(index);
        if (c == '\n' || c == '\r' || c == '\t') {
            c = ' ';
        }
        if (c == '"' || c == '\'' || c == '[' || c == ']' || c == '{' || c == '}' || c == '<' || c == '>') {
            continue;
        }
        cleaned += c;
    }
    cleaned.trim();
    if (cleaned.length() > 180) {
        cleaned = cleaned.substring(0, 180);
    }
    return cleaned;
}

}  // namespace

bool AudioOutput::begin() {
    if (!M5.Speaker.isEnabled()) {
        Serial.println("[VoiceTrace] AUDIO: speaker disabled (pin not configured)");
        return false;
    }

    if (!M5.Speaker.isRunning()) {
        const bool started = M5.Speaker.begin();
        Serial.printf("[VoiceTrace] AUDIO: speaker begin=%s\n", started ? "ok" : "fail");
    }

    M5.Speaker.setVolume(clampVolume(volume_));
    Serial.printf("[VoiceTrace] AUDIO: speaker enabled=%d running=%d volume=%u\n",
                  static_cast<int>(M5.Speaker.isEnabled()),
                  static_cast<int>(M5.Speaker.isRunning()),
                  static_cast<unsigned>(volume_));

    if (!neuralTts_.begin()) {
        Serial.println("[AudioOutput] Neural TTS worker unavailable");
    }

    Serial.println("[VoiceTrace] VOICE: backend=Piper");

    return true;
}

bool AudioOutput::isSpeaking() const {
    return neuralTts_.isBusy() || M5.Speaker.isPlaying();
}

bool AudioOutput::playWavFromSd(const String& path) {
    if (!M5.Speaker.isEnabled() || !SdManager::isMounted()) {
        return false;
    }

    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        return false;
    }

    WavFormatInfo format{};
    if (!parseWavHeader(file, format)) {
        Serial.printf("[VoiceTrace] VOICE: invalid_wav=%s\n", path.c_str());
        file.close();
        return false;
    }

    if (!file.seek(format.dataOffset)) {
        file.close();
        return false;
    }

    constexpr uint32_t kWholeClipMaxBytes = 196608;
    if (format.dataSize > 0 && format.dataSize <= kWholeClipMaxBytes) {
        uint8_t* wholeBuffer = static_cast<uint8_t*>(malloc(static_cast<size_t>(format.dataSize)));
        if (wholeBuffer != nullptr) {
            const int bytesRead = file.read(wholeBuffer, format.dataSize);
            if (bytesRead == static_cast<int>(format.dataSize)) {
                const size_t sampleCount = static_cast<size_t>(bytesRead) / sizeof(int16_t);
                if (sampleCount > 0) {
                    applyHeadroomAndLimiter(reinterpret_cast<int16_t*>(wholeBuffer), sampleCount, true, true);

                    while (M5.Speaker.isPlaying(0)) {
                        delay(1);
                    }

                    bool queued = false;
                    while (!queued) {
                        queued = M5.Speaker.playRaw(reinterpret_cast<const int16_t*>(wholeBuffer),
                                                    sampleCount,
                                                    format.sampleRate,
                                                    false,
                                                    1,
                                                    0,
                                                    false);
                        if (!queued) {
                            delay(1);
                        }
                    }

                    while (M5.Speaker.isPlaying(0)) {
                        delay(1);
                    }

                    free(wholeBuffer);
                    file.close();
                    return true;
                }
            }
            free(wholeBuffer);
            if (!file.seek(format.dataOffset)) {
                file.close();
                return false;
            }
        }
    }

    size_t bufferIndex = 0;
    uint32_t remaining = format.dataSize;

    // Avoid hard stop pops by waiting for any previous stream to drain naturally.
    while (M5.Speaker.isPlaying(0)) {
        delay(1);
    }

    while (remaining > 0) {
        const size_t toRead = remaining > kWavChunkBytes ? kWavChunkBytes : remaining;
        const int readBytes = file.read(gWavBuffers[bufferIndex], toRead);
        if (readBytes <= 0) {
            break;
        }

        const size_t sampleCount = static_cast<size_t>(readBytes) / sizeof(int16_t);
        if (sampleCount > 0) {
            const bool firstChunk = (remaining == format.dataSize);
            const bool lastChunk = (static_cast<uint32_t>(readBytes) >= remaining);
            applyHeadroomAndLimiter(reinterpret_cast<int16_t*>(gWavBuffers[bufferIndex]), sampleCount, firstChunk, lastChunk);
            bool queued = false;
            while (!queued) {
                queued = M5.Speaker.playRaw(reinterpret_cast<const int16_t*>(gWavBuffers[bufferIndex]),
                                            sampleCount,
                                            format.sampleRate,
                                            false,
                                            1,
                                            0,
                                            false);
                if (!queued) {
                    delay(1);
                }
            }
        }

        remaining -= static_cast<uint32_t>(readBytes);
        bufferIndex = (bufferIndex + 1) % kWavBufferCount;
        yield();
    }

    // Block until queued chunks finish to avoid clipping the tail.
    while (M5.Speaker.isPlaying(0)) {
        delay(1);
    }

    file.close();
    return remaining == 0;
}

void AudioOutput::setVolume(uint8_t volume) {
    volume_ = clampVolume(volume);
    if (M5.Speaker.isEnabled()) {
        M5.Speaker.setVolume(volume_);
    }
}

void AudioOutput::setVoiceStyle(const String& style) {
    String normalized = style;
    normalized.trim();
    normalized.toLowerCase();

    if (normalized == "clear") {
        voiceStyle_ = VoiceStyle::Clear;
    } else if (normalized == "bright") {
        voiceStyle_ = VoiceStyle::Bright;
    } else if (normalized == "deep") {
        voiceStyle_ = VoiceStyle::Deep;
    } else if (normalized == "warm") {
        voiceStyle_ = VoiceStyle::Warm;
    } else {
        voiceStyle_ = VoiceStyle::Natural;
    }

    Serial.printf("[AudioOutput] Voice style set: %s\n", voiceStyleName().c_str());
}

String AudioOutput::voiceStyleName() const {
    if (voiceStyle_ == VoiceStyle::Clear) {
        return "clear";
    }
    if (voiceStyle_ == VoiceStyle::Bright) {
        return "bright";
    }
    if (voiceStyle_ == VoiceStyle::Deep) {
        return "deep";
    }
    if (voiceStyle_ == VoiceStyle::Warm) {
        return "warm";
    }
    return "natural";
}

std::vector<String> AudioOutput::listVoices() const {
    return neuralTts_.listVoices();
}

bool AudioOutput::setActiveVoiceModel(const String& modelFileName) {
    return neuralTts_.setActiveVoice(modelFileName);
}

String AudioOutput::activeVoiceModel() const {
    return neuralTts_.activeVoice();
}

void AudioOutput::setVoiceTuning(float speed, float pitch, float clarity) {
    neuralTts_.setVoiceParams(speed, pitch, clarity);
}

float AudioOutput::voiceSpeed() const {
    return neuralTts_.speed();
}

float AudioOutput::voicePitch() const {
    return neuralTts_.pitch();
}

float AudioOutput::voiceClarity() const {
    return neuralTts_.clarity();
}

void AudioOutput::setAmplitudeEnvelopeHandler(AmplitudeEnvelopeFn handler, void* context) {
    neuralTts_.setAmplitudeEnvelopeHandler(handler, context);
}

void AudioOutput::update() {
    // Non-blocking audio streaming update can be added here for advanced features
    // For now, we stream blocking during speakTTS()
}

void AudioOutput::speakPiper(const String& msg,
                            const String& emotion,
                            const String& lang,
                            float speedScale,
                            float pitchScale) {
    const float effectiveSpeed = voiceSpeed() * (speedScale <= 0.0f ? 1.0f : speedScale);
    const float effectivePitch = voicePitch() * (pitchScale <= 0.0f ? 1.0f : pitchScale);
    neuralTts_.setVoiceParams(effectiveSpeed, effectivePitch, voiceClarity());

    if (!M5.Speaker.isEnabled()) {
        Serial.println("[AudioOutput] Speaker not enabled");
        return;
    }

    if (msg.length() == 0) {
        return;
    }

    String speechText = sanitizeSpeechText(msg);
    if (speechText.length() == 0) {
        playEmotionTone(emotion);
        return;
    }

    if (!neuralTts_.speak(speechText, emotion, lang)) {
        Serial.println("[AudioOutput] Piper queue unavailable, playing tone");
        playEmotionTone(emotion);
        return;
    }

    Serial.printf("[VoiceTrace] VOICE: Piper queued text=%s\n", speechText.c_str());
}

void AudioOutput::speakTTS(const String& msg, const String& emotion, const String& lang) {
    speakPiper(msg, emotion, lang, 1.0f, 1.0f);
}

void AudioOutput::playEmotionTone(const String& emotion) {
    if (!M5.Speaker.isEnabled()) {
        return;
    }

    if (!M5.Speaker.isRunning()) {
        M5.Speaker.begin();
        M5.Speaker.setVolume(clampVolume(volume_));
    }

    SdDiagnostics::logSoundPlay(String("tone:") + emotion);

    float freq = 420.0f;
    uint16_t durationMs = 80;

    if (emotion == "happy") {
        freq = 880.0f;
        durationMs = 90;
    } else if (emotion == "warning" || emotion == "surprised") {
        freq = 220.0f;
        durationMs = 140;
    } else if (emotion == "curious") {
        freq = 520.0f;
        durationMs = 90;
    } else if (emotion == "sleepy") {
        freq = 280.0f;
        durationMs = 120;
    }

    M5.Speaker.tone(freq, durationMs);
}

}  // namespace Flic
