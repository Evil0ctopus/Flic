#include "audio_output.h"

#include "../diagnostics/sd_diagnostics.h"
#include "../subsystems/sd_manager.h"

#include <M5Unified.h>
#include <ESP8266SAM.h>
#include <AudioOutput.h>
#include <SD.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace Flic {

namespace {
constexpr uint8_t kSpeechOutputVolume = 120;
constexpr uint32_t kTtsLongSpeechWarnMs = 2500;

struct TtsRequest {
    char text[224];
    char emotion[24];
    char lang[8];
    uint32_t queuedAtMs = 0;
};

void processSpeechRequest(AudioOutput* owner, const TtsRequest& request);
bool samFallbackSpeak(const String& text,
                      const String& emotion,
                      const String& lang,
                      float speed,
                      float pitch,
                      float clarity,
                      void* context);

uint8_t clampVolume(uint8_t volume) {
    return volume;
}

class AudioOutputM5Speaker : public ::AudioOutput {
public:
    explicit AudioOutputM5Speaker(m5::Speaker_Class* speaker, uint8_t virtualChannel = 0)
        : speaker_(speaker), virtualChannel_(virtualChannel) {}

    bool begin() override {
        bufferIndex_ = 0;
        triIndex_ = 0;
        if (speaker_ != nullptr) {
            speaker_->setVolume(kSpeechOutputVolume);
        }
        return true;
    }

    bool ConsumeSample(int16_t sample[2]) override {
        if ((bufferIndex_ + 1) >= triBufSize_) {
            flush();
        }

        if ((bufferIndex_ + 1) < triBufSize_) {
            triBuffer_[triIndex_][bufferIndex_] = sample[0];
            triBuffer_[triIndex_][bufferIndex_ + 1] = sample[1];
            bufferIndex_ += 2;
            return true;
        }

        return false;
    }

    void flush() override {
        if (speaker_ != nullptr && bufferIndex_ > 0) {
            yield();
            speaker_->playRaw(triBuffer_[triIndex_], bufferIndex_, hertz, true, 1, virtualChannel_);
            triIndex_ = triIndex_ < 2 ? triIndex_ + 1 : 0;
            bufferIndex_ = 0;
            yield();
        }
    }

    bool stop() override {
        flush();
        if (speaker_ != nullptr) {
            speaker_->stop(virtualChannel_);
        }
        return true;
    }

private:
    static constexpr size_t triBufSize_ = 2048;
    static int16_t triBuffer_[3][triBufSize_];
    m5::Speaker_Class* speaker_ = nullptr;
    uint8_t virtualChannel_ = 0;
    size_t bufferIndex_ = 0;
    size_t triIndex_ = 0;
};

int16_t AudioOutputM5Speaker::triBuffer_[3][AudioOutputM5Speaker::triBufSize_] = {};

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

float clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

String normalizeEmotionName(const String& emotion) {
    String normalized = emotion;
    normalized.trim();
    normalized.toLowerCase();
    if (normalized.length() == 0) {
        normalized = "neutral";
    }
    return normalized;
}

uint32_t readLe32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

uint16_t readLe16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

bool readWavFile(const String& path, std::vector<int16_t>& outSamples, uint32_t& outSampleRate) {
    outSamples.clear();
    outSampleRate = 22050;

    if (!SD.exists(path)) {
        return false;
    }

    File wav = SD.open(path, FILE_READ);
    if (!wav) {
        return false;
    }

    const size_t size = static_cast<size_t>(wav.size());
    if (size < 44 || size > (512 * 1024)) {
        wav.close();
        return false;
    }

    std::vector<uint8_t> fileBytes(size);
    if (wav.read(fileBytes.data(), size) != size) {
        wav.close();
        return false;
    }
    wav.close();

    if (memcmp(fileBytes.data(), "RIFF", 4) != 0 || memcmp(fileBytes.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    uint16_t numChannels = 1;
    uint16_t bitsPerSample = 16;
    uint32_t sampleRate = 22050;
    const uint8_t* dataChunk = nullptr;
    size_t dataChunkSize = 0;

    size_t offset = 12;
    while (offset + 8 <= fileBytes.size()) {
        const uint8_t* chunk = fileBytes.data() + offset;
        const uint32_t chunkSize = readLe32(chunk + 4);
        const size_t payloadStart = offset + 8;
        const size_t payloadEnd = payloadStart + chunkSize;
        if (payloadEnd > fileBytes.size()) {
            break;
        }

        if (memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
            const uint8_t* fmt = fileBytes.data() + payloadStart;
            const uint16_t audioFormat = readLe16(fmt + 0);
            numChannels = readLe16(fmt + 2);
            sampleRate = readLe32(fmt + 4);
            bitsPerSample = readLe16(fmt + 14);
            if (audioFormat != 1) {
                return false;
            }
        } else if (memcmp(chunk, "data", 4) == 0) {
            dataChunk = fileBytes.data() + payloadStart;
            dataChunkSize = chunkSize;
        }

        offset = payloadEnd + (chunkSize & 1U);
    }

    if (dataChunk == nullptr || dataChunkSize < 2 || bitsPerSample != 16 || (numChannels != 1 && numChannels != 2)) {
        return false;
    }

    const size_t frameBytes = static_cast<size_t>(numChannels) * sizeof(int16_t);
    const size_t frameCount = dataChunkSize / frameBytes;
    outSamples.resize(frameCount);

    for (size_t i = 0; i < frameCount; ++i) {
        const uint8_t* frame = dataChunk + (i * frameBytes);
        if (numChannels == 1) {
            outSamples[i] = static_cast<int16_t>(readLe16(frame));
        } else {
            const int16_t left = static_cast<int16_t>(readLe16(frame));
            const int16_t right = static_cast<int16_t>(readLe16(frame + 2));
            outSamples[i] = static_cast<int16_t>((static_cast<int32_t>(left) + static_cast<int32_t>(right)) / 2);
        }
    }

    outSampleRate = sampleRate > 0 ? sampleRate : 22050;
    return !outSamples.empty();
}

bool loadCreatureWav(const char* name, std::vector<int16_t>& outSamples, uint32_t& outSampleRate) {
    if (name == nullptr || name[0] == '\0' || !SdManager::isMounted()) {
        return false;
    }

    String fileName = String(name);
    if (!fileName.endsWith(".wav")) {
        fileName += ".wav";
    }

    const String candidates[] = {
        String("/Flic/sounds/creature/") + fileName,
        String("/sounds/creature/") + fileName,
    };

    for (const String& path : candidates) {
        if (readWavFile(path, outSamples, outSampleRate)) {
            return true;
        }
    }

    return false;
}

void synthCreaturePcm(const String& text,
                      const String& emotion,
                      float chaosLevel,
                      std::vector<int16_t>& pcm,
                      uint32_t sampleRate) {
    pcm.clear();
    const uint32_t safeRate = sampleRate == 0 ? 22050 : sampleRate;
    const float baseHz = 196.0f;
    float phase = 0.0f;

    for (size_t idx = 0; idx < static_cast<size_t>(text.length()); ++idx) {
        const char c = text.charAt(idx);
        const bool isSpace = c == ' ';
        const bool isVowel = c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ||
                             c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U';
        const bool hardStop = c == '.' || c == '!' || c == '?';

        const size_t glyphSamples = hardStop ? 1300 : (isSpace ? 320 : (isVowel ? 860 : 640));
        float pitchMul = isVowel ? 1.16f : 0.94f;
        if (emotion == "sleepy") {
            pitchMul *= 0.86f;
        } else if (emotion == "happy") {
            pitchMul *= 1.08f;
        } else if (emotion == "mischievous" || emotion == "curious" || emotion == "surprised" || emotion == "excited") {
            pitchMul *= 1.12f;
        }

        const float freqHz = baseHz * pitchMul;
        const float phaseStep = 2.0f * static_cast<float>(M_PI) * freqHz / static_cast<float>(safeRate);
        for (size_t i = 0; i < glyphSamples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(glyphSamples);
            const float env = (t < 0.12f) ? (t / 0.12f) : ((t > 0.84f) ? ((1.0f - t) / 0.16f) : 1.0f);
            const float chaotic = std::sin((phase * (2.3f + chaosLevel * 2.0f)) + t * 12.0f) * (0.08f + chaosLevel * 0.18f);
            float s = std::sin(phase) * (0.62f + (isVowel ? 0.18f : 0.0f));
            s += std::sin(phase * 2.0f) * 0.22f;
            s += chaotic;
            s *= env * 0.55f;
            const int32_t q = static_cast<int32_t>(s * 32767.0f);
            pcm.push_back(static_cast<int16_t>(q < -32768 ? -32768 : (q > 32767 ? 32767 : q)));
            phase += phaseStep;
            if (phase > (2.0f * static_cast<float>(M_PI))) {
                phase -= 2.0f * static_cast<float>(M_PI);
            }
        }
    }

    if (pcm.empty()) {
        pcm.push_back(0);
    }
}

void playCreatureChatterTones(const String& text, const String& emotion) {
    if (!M5.Speaker.isEnabled()) {
        return;
    }

    const bool chaotic = (emotion == "mischievous" || emotion == "curious" || emotion == "surprised" || emotion == "excited");
    const bool sleepy = (emotion == "sleepy");
    const int base = sleepy ? 260 : 420;
    const int spread = chaotic ? 520 : 300;
    const int toneMs = sleepy ? 24 : 18;

    const size_t steps = text.length() < 10 ? 10 : (text.length() > 28 ? 28 : static_cast<size_t>(text.length()));
    for (size_t i = 0; i < steps; ++i) {
        const char c = text.charAt(i % text.length());
        int freq = base + (static_cast<int>(c) % spread);
        if (chaotic) {
            freq += (i % 3 == 0) ? 90 : ((i % 4 == 0) ? -60 : 20);
        }
        if (freq < 180) {
            freq = 180;
        }
        if (freq > 1400) {
            freq = 1400;
        }
        M5.Speaker.tone(static_cast<float>(freq), static_cast<uint16_t>(toneMs));
        delay(6);
    }
}

void processSpeechRequest(AudioOutput* owner, const TtsRequest& request) {
    if (owner == nullptr) {
        return;
    }

    const unsigned long startMs = millis();
    Serial.printf("[AudioOutput] TTS start (queued %lu ms ago): %s\n",
                  static_cast<unsigned long>(startMs - request.queuedAtMs), request.text);

    AudioOutputM5Speaker audioOut(&M5.Speaker, 0);
    if (!audioOut.begin()) {
        Serial.println("[AudioOutput] Failed to initialize audio output");
        owner->playEmotionTone(String(request.emotion));
        return;
    }

    ESP8266SAM sam;

    uint8_t speed = 58;
    uint8_t pitch = 50;
    uint8_t mouth = 138;
    uint8_t throat = 118;
    const String emotion = String(request.emotion);
    const String styleName = owner->voiceStyleName();

    if (styleName == "clear") {
        sam.SetVoice(ESP8266SAM::VOICE_SAM);
        speed = 61;
        pitch = 56;
        mouth = 150;
        throat = 110;
    } else if (styleName == "bright") {
        sam.SetVoice(ESP8266SAM::VOICE_ELF);
        speed = 62;
        pitch = 58;
        mouth = 146;
        throat = 116;
    } else if (styleName == "deep") {
        sam.SetVoice(ESP8266SAM::VOICE_SAM);
        speed = 56;
        pitch = 46;
        mouth = 126;
        throat = 140;
    } else if (styleName == "warm") {
        sam.SetVoice(ESP8266SAM::VOICE_OLDLADY);
        speed = 57;
        pitch = 49;
        mouth = 132;
        throat = 124;
    } else {
        sam.SetVoice(ESP8266SAM::VOICE_OLDLADY);
    }

    if (emotion == "happy") {
        speed = static_cast<uint8_t>(speed + 3);
        pitch = static_cast<uint8_t>(pitch + 3);
    } else if (emotion == "curious") {
        speed = static_cast<uint8_t>(speed + 2);
        pitch = static_cast<uint8_t>(pitch + 2);
    } else if (emotion == "warning" || emotion == "surprised") {
        speed = static_cast<uint8_t>(speed + 2);
        pitch = static_cast<uint8_t>(pitch > 3 ? pitch - 3 : pitch);
    } else if (emotion == "sleepy") {
        speed = static_cast<uint8_t>(speed > 5 ? speed - 5 : speed);
        pitch = static_cast<uint8_t>(pitch > 4 ? pitch - 4 : pitch);
    }

    const float speedTuning = owner->voiceSpeed();
    const float pitchTuning = owner->voicePitch();
    const float clarityTuning = owner->voiceClarity();

    speed = static_cast<uint8_t>(speed * speedTuning);
    pitch = static_cast<uint8_t>(pitch * pitchTuning);
    mouth = static_cast<uint8_t>(mouth * clarityTuning);

    sam.SetSpeed(speed < 10 ? 10 : (speed > 120 ? 120 : speed));
    sam.SetPitch(pitch < 10 ? 10 : (pitch > 120 ? 120 : pitch));
    sam.SetMouth(mouth);
    sam.SetThroat(throat);

    yield();
    const bool speechOk = sam.Say(&audioOut, request.text);
    yield();

    if (!speechOk) {
        Serial.println("[AudioOutput] SAM speech failed, falling back to tone");
        owner->playEmotionTone(emotion);
    } else {
        const unsigned long elapsedMs = millis() - startMs;
        Serial.printf("[AudioOutput] TTS end (%lu ms)\n", static_cast<unsigned long>(elapsedMs));
        if (elapsedMs > kTtsLongSpeechWarnMs) {
            Serial.printf("[AudioOutput] Warning: TTS ran long (%lu ms)\n", static_cast<unsigned long>(elapsedMs));
        }
    }

    audioOut.stop();
}

bool samFallbackSpeak(const String& text,
                      const String& emotion,
                      const String& lang,
                      float speed,
                      float pitch,
                      float clarity,
                      void* context) {
    (void)speed;
    (void)pitch;
    (void)clarity;
    auto* owner = static_cast<AudioOutput*>(context);
    if (owner == nullptr) {
        return false;
    }

    TtsRequest request{};
    snprintf(request.text, sizeof(request.text), "%s", text.c_str());
    snprintf(request.emotion, sizeof(request.emotion), "%s", emotion.c_str());
    snprintf(request.lang, sizeof(request.lang), "%s", lang.c_str());
    request.queuedAtMs = millis();
    processSpeechRequest(owner, request);
    return true;
}

}  // namespace

bool AudioOutput::begin() {
    if (!M5.Speaker.isEnabled()) {
        return false;
    }

    M5.Speaker.setVolume(clampVolume(volume_));

    if (!neuralTts_.begin(samFallbackSpeak, this)) {
        Serial.println("[AudioOutput] Neural TTS worker unavailable; fallback voice may be limited");
    }

    return true;
}

bool AudioOutput::isSpeaking() const {
    return neuralTts_.isBusy();
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

void AudioOutput::setFallbackVoiceEnabled(bool enabled) {
    neuralTts_.setFallbackEnabled(enabled);
}

bool AudioOutput::fallbackVoiceEnabled() const {
    return neuralTts_.fallbackEnabled();
}

void AudioOutput::setAmplitudeEnvelopeHandler(AmplitudeEnvelopeFn handler, void* context) {
    neuralTts_.setAmplitudeEnvelopeHandler(handler, context);
}

void AudioOutput::update() {
    // Non-blocking audio streaming update can be added here for advanced features
    // For now, we stream blocking during speakTTS()
}

void AudioOutput::speakTTS(const String& msg, const String& emotion, const String& lang) {
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
        Serial.println("[AudioOutput] Neural TTS queue full/unavailable, falling back to tone");
        playEmotionTone(emotion);
        return;
    }

    Serial.printf("[AudioOutput] TTS queued: %s (emotion: %s, lang: %s, style: %s, voice: %s)\n",
                  speechText.c_str(), emotion.c_str(), lang.c_str(), voiceStyleName().c_str(), activeVoiceModel().c_str());
}

void AudioOutput::speakCreatureTTS(const char* text, const char* emotion, const char* lang) {
    if (!M5.Speaker.isEnabled() || text == nullptr) {
        Serial.println("[VoiceTrace] creature tts aborted: speaker disabled or null text");
        return;
    }

    String speechText = sanitizeSpeechText(String(text));
    if (speechText.length() == 0) {
        Serial.println("[VoiceTrace] creature tts aborted: empty sanitized text");
        return;
    }

    const String emotionName = normalizeEmotionName(String(emotion == nullptr ? "neutral" : emotion));
    setCreatureEmotion(emotionName);
    Serial.printf("[VoiceTrace] creature tts start emotion=%s text=\"%s\"\n",
                  emotionName.c_str(), speechText.c_str());

    // Guaranteed audible fallback signature for creature speech events.
    playCreatureChatterTones(speechText, emotionName);

    const String safeLang = (lang == nullptr || strlen(lang) == 0) ? String("en") : String(lang);

    // Force guaranteed audible base speech path for creature mode.
    // This bypasses the neural queue to avoid silent queue/no-output edge cases.
    samFallbackSpeak(speechText, emotionName, safeLang, voiceSpeed(), voicePitch(), voiceClarity(), this);
    Serial.println("[VoiceTrace] base speech path: samFallbackSpeak done");

    std::vector<int16_t> pcm;
    constexpr uint32_t kSampleRate = 22050;
    synthCreaturePcm(speechText, emotionName, dspChaosLevel_, pcm, kSampleRate);

    const bool spikeEmotion = (emotionName == "mischievous" || emotionName == "curious" ||
                               emotionName == "surprised" || emotionName == "excited");
    if (spikeEmotion) {
        const char* micro = (emotionName == "mischievous") ? "hah" : ((emotionName == "surprised") ? "eep" : "hmm");
        std::vector<int16_t> microSamples;
        uint32_t microRate = 0;
        if (loadCreatureWav(micro, microSamples, microRate) && !microSamples.empty()) {
            const size_t mixCount = std::min(pcm.size(), microSamples.size());
            for (size_t i = 0; i < mixCount; ++i) {
                const int32_t mixed = static_cast<int32_t>(pcm[i]) + static_cast<int32_t>(microSamples[i] * 0.25f);
                pcm[i] = static_cast<int16_t>(mixed < -32768 ? -32768 : (mixed > 32767 ? 32767 : mixed));
            }
        }
    }

    processCreatureDSP(pcm.data(), pcm.size());
    Serial.printf("[VoiceTrace] dsp processed samples=%u\n", static_cast<unsigned>(pcm.size()));
    constexpr size_t kChunk = 1024;
    size_t chunks = 0;
    M5.Speaker.setVolume(clampVolume(volume_));
    for (size_t offset = 0; offset < pcm.size(); offset += kChunk) {
        const size_t chunkSamples = std::min(kChunk, pcm.size() - offset);
        M5.Speaker.playRaw(&pcm[offset], chunkSamples, kSampleRate, false, 1, 0);
        ++chunks;
    }
    Serial.printf("[VoiceTrace] speaker write complete chunks=%u rate=%u\n",
                  static_cast<unsigned>(chunks), static_cast<unsigned>(kSampleRate));
}

void AudioOutput::setCreatureEmotion(const String& emotion) {
    creatureEmotion_ = normalizeEmotionName(emotion);

    dspPitchSemitones_ = 0.0f;
    dspFormantShift_ = 1.00f;
    dspDistortionDrive_ = 1.08f;
    dspEqBoost_ = 1.06f;
    dspCompressorThreshold_ = 0.66f;
    dspCompressorRatio_ = 2.2f;
    dspLowPassCutoffHz_ = 9200.0f;
    dspChaosLevel_ = 0.03f;

    if (creatureEmotion_ == "happy") {
        dspPitchSemitones_ = 1.5f;
        dspFormantShift_ = 1.04f;
        dspDistortionDrive_ = 1.10f;
        dspEqBoost_ = 1.10f;
        dspChaosLevel_ = 0.06f;
    } else if (creatureEmotion_ == "curious") {
        dspPitchSemitones_ = 3.2f;
        dspFormantShift_ = 1.10f;
        dspDistortionDrive_ = 1.16f;
        dspEqBoost_ = 1.15f;
        dspChaosLevel_ = 0.22f;
    } else if (creatureEmotion_ == "mischievous") {
        dspPitchSemitones_ = 4.8f;
        dspFormantShift_ = 1.14f;
        dspDistortionDrive_ = 1.26f;
        dspEqBoost_ = 1.19f;
        dspCompressorThreshold_ = 0.60f;
        dspCompressorRatio_ = 2.8f;
        dspChaosLevel_ = 0.36f;
    } else if (creatureEmotion_ == "excited") {
        dspPitchSemitones_ = 4.2f;
        dspFormantShift_ = 1.12f;
        dspDistortionDrive_ = 1.22f;
        dspEqBoost_ = 1.18f;
        dspChaosLevel_ = 0.30f;
    } else if (creatureEmotion_ == "surprised") {
        dspPitchSemitones_ = 5.0f;
        dspFormantShift_ = 1.16f;
        dspDistortionDrive_ = 1.28f;
        dspEqBoost_ = 1.22f;
        dspChaosLevel_ = 0.40f;
    } else if (creatureEmotion_ == "sleepy") {
        dspPitchSemitones_ = -3.2f;
        dspFormantShift_ = 0.88f;
        dspDistortionDrive_ = 1.02f;
        dspEqBoost_ = 0.96f;
        dspCompressorThreshold_ = 0.70f;
        dspCompressorRatio_ = 1.8f;
        dspLowPassCutoffHz_ = 8200.0f;
        dspChaosLevel_ = 0.0f;
    }
}

String AudioOutput::creatureEmotion() const {
    return creatureEmotion_;
}

void AudioOutput::processCreatureDSP(int16_t* samples, size_t count) {
    if (samples == nullptr || count == 0) {
        Serial.println("[VoiceTrace] dsp skipped: empty buffer");
        return;
    }

    constexpr float kSampleRate = 22050.0f;
    std::vector<int16_t> source(samples, samples + count);

    float pitchRatio = std::pow(2.0f, dspPitchSemitones_ / 12.0f);
    pitchRatio = clampFloat(pitchRatio, 0.60f, 1.50f);
    float readPos = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        const size_t idx = static_cast<size_t>(readPos);
        const float frac = readPos - static_cast<float>(idx);
        const int16_t a = source[idx < source.size() ? idx : (source.size() - 1)];
        const int16_t b = source[(idx + 1) < source.size() ? (idx + 1) : (source.size() - 1)];
        const float interp = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * frac;
        samples[i] = static_cast<int16_t>(interp);
        readPos += pitchRatio;
        if (readPos >= static_cast<float>(source.size() - 1)) {
            readPos = static_cast<float>(source.size() - 1);
        }
    }

    float lpState = dspLowPassState_;
    const float rc = 1.0f / (2.0f * static_cast<float>(M_PI) * dspLowPassCutoffHz_);
    const float dt = 1.0f / kSampleRate;
    const float lpAlpha = dt / (rc + dt);
    float formantLp = 0.0f;

    for (size_t i = 0; i < count; ++i) {
        float x = static_cast<float>(samples[i]) / 32768.0f;

        formantLp += (x - formantLp) * 0.12f;
        const float formantHp = x - formantLp;
        x = (formantLp * (2.0f - dspFormantShift_)) + (formantHp * dspFormantShift_);

        x = std::tanh(x * dspDistortionDrive_);

        const float eqBand = x - formantLp;
        x += eqBand * (dspEqBoost_ - 1.0f) * 0.8f;

        const float absx = std::fabs(x);
        if (absx > dspCompressorThreshold_) {
            const float sign = x >= 0.0f ? 1.0f : -1.0f;
            const float exceed = absx - dspCompressorThreshold_;
            x = sign * (dspCompressorThreshold_ + exceed / dspCompressorRatio_);
        }

        lpState += lpAlpha * (x - lpState);
        x = lpState;

        const int32_t q = static_cast<int32_t>(x * 32767.0f);
        samples[i] = static_cast<int16_t>(q < -32768 ? -32768 : (q > 32767 ? 32767 : q));
    }

    dspLowPassState_ = lpState;
}

void AudioOutput::playEmotionTone(const String& emotion) {
    if (!M5.Speaker.isEnabled()) {
        return;
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


void AudioOutput::playCreatureSound(const String& sound) {
    if (!playCreatureSound(sound.c_str())) {
        if (!M5.Speaker.isEnabled()) {
            return;
        }
        if (sound == "purr") {
            M5.Speaker.tone(180.0f, 90);
        } else if (sound == "chirp") {
            M5.Speaker.tone(920.0f, 45);
        } else if (sound == "tick") {
            M5.Speaker.tone(720.0f, 30);
        } else {
            M5.Speaker.tone(500.0f, 70);
        }
    }
}

bool AudioOutput::playCreatureSound(const char* soundName) {
    if (!M5.Speaker.isEnabled() || soundName == nullptr) {
        return false;
    }

    SdDiagnostics::logSoundPlay(String("creature:") + String(soundName));

    std::vector<int16_t> samples;
    uint32_t sampleRate = 0;
    if (!loadCreatureWav(soundName, samples, sampleRate) || samples.empty()) {
        return false;
    }

    processCreatureDSP(samples.data(), samples.size());
    constexpr size_t kChunk = 1024;
    M5.Speaker.setVolume(clampVolume(volume_));
    for (size_t offset = 0; offset < samples.size(); offset += kChunk) {
        const size_t chunkSamples = std::min(kChunk, samples.size() - offset);
        M5.Speaker.playRaw(&samples[offset], chunkSamples, sampleRate, false, 1, 0);
    }
    return true;
}

}  // namespace Flic
